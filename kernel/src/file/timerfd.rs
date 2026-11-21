use alloc::{
    borrow::{Cow, ToOwned},
    collections::BTreeMap,
    sync::{Arc, Weak},
};
use core::{
    sync::atomic::{AtomicBool, Ordering},
    task::Context,
    time::Duration,
};

use axerrno::{AxError, AxResult};
use axhal::time::monotonic_time;
use axpoll::{IoEvents, PollSet, Pollable};
use axtask::{
    AxCpuMask,
    future::{block_on, poll_io, timeout},
    set_current_affinity,
};
use event_listener::{Event, listener};
use kspin::SpinNoPreempt;

use super::{FileLike, IoDst};

type TimerKey = (Duration, usize);
static TIMERS: SpinNoPreempt<BTreeMap<TimerKey, Weak<TimerFd>>> =
    SpinNoPreempt::new(BTreeMap::new());
static SCHEDULE_CHANGED: Event = Event::new();

pub struct TimerFd {
    // An expired deadline remains readable until it is consumed or reset.
    deadline: SpinNoPreempt<Option<Duration>>,
    nonblocking: AtomicBool,
    readable: PollSet,
}

impl TimerFd {
    pub fn new(nonblocking: bool) -> Self {
        Self {
            deadline: SpinNoPreempt::new(None),
            nonblocking: AtomicBool::new(nonblocking),
            readable: PollSet::new(),
        }
    }

    fn key(&self, deadline: Duration) -> TimerKey {
        (deadline, self as *const Self as usize)
    }

    pub fn settime(self: &Arc<Self>, value: Duration, absolute: bool) -> AxResult<Duration> {
        let mut current = self.deadline.lock();
        let now = monotonic_time();
        let deadline = if value.is_zero() {
            None
        } else if absolute {
            Some(value)
        } else {
            Some(now.checked_add(value).ok_or(AxError::InvalidInput)?)
        };
        let mut timers = TIMERS.lock();
        if let Some(old) = *current {
            timers.remove(&self.key(old));
        }
        if let Some(next) = deadline.filter(|deadline| *deadline > now) {
            timers.insert(self.key(next), Arc::downgrade(self));
        }
        let previous = core::mem::replace(&mut *current, deadline);
        drop(timers);
        drop(current);
        if deadline.is_some_and(|deadline| deadline <= now) {
            self.readable.wake();
        }
        SCHEDULE_CHANGED.notify(1);
        Ok(previous.unwrap_or_default().saturating_sub(now))
    }

    pub fn remaining(&self) -> Duration {
        self.deadline
            .lock()
            .unwrap_or_default()
            .saturating_sub(monotonic_time())
    }

    fn expired(&self) -> bool {
        self.deadline
            .lock()
            .is_some_and(|deadline| deadline <= monotonic_time())
    }
}

impl Drop for TimerFd {
    fn drop(&mut self) {
        if let Some(deadline) = *self.deadline.get_mut() {
            TIMERS.lock().remove(&self.key(deadline));
            SCHEDULE_CHANGED.notify(1);
        }
    }
}

impl FileLike for TimerFd {
    fn read(&self, dst: &mut IoDst) -> AxResult<usize> {
        if dst.remaining_mut() < size_of::<u64>() {
            return Err(AxError::InvalidInput);
        }
        block_on(poll_io(self, IoEvents::IN, self.nonblocking(), || {
            let mut current = self.deadline.lock();
            let deadline = current
                .filter(|deadline| *deadline <= monotonic_time())
                .ok_or(AxError::WouldBlock)?;
            TIMERS.lock().remove(&self.key(deadline));
            *current = None;
            // A user copy may fault and enter the scheduler.
            drop(current);
            dst.write(&1u64.to_ne_bytes())?;
            Ok(size_of::<u64>())
        }))
    }

    fn nonblocking(&self) -> bool {
        self.nonblocking.load(Ordering::Acquire)
    }

    fn set_nonblocking(&self, value: bool) -> AxResult {
        self.nonblocking.store(value, Ordering::Release);
        Ok(())
    }

    fn path(&self) -> Cow<'_, str> {
        "anon_inode:[timerfd]".into()
    }
}

impl Pollable for TimerFd {
    fn poll(&self) -> IoEvents {
        if self.expired() {
            IoEvents::IN
        } else {
            IoEvents::empty()
        }
    }

    fn register(&self, cx: &mut Context<'_>, events: IoEvents) {
        if events.contains(IoEvents::IN) {
            self.readable.register(cx.waker());
        }
    }
}

async fn dispatch_timers() {
    loop {
        // Subscribe before looking at the schedule so an earlier rearm or
        // cancellation cannot be lost between the lookup and sleep.
        listener!(SCHEDULE_CHANGED => changed);
        let mut timers = TIMERS.lock();
        let now = monotonic_time();
        let next = timers.first_key_value().map(|(key, _)| key.0);
        let expired = if next.is_some_and(|deadline| deadline <= now) {
            timers.pop_first()
        } else {
            None
        };
        // File operations take the descriptor lock before the schedule lock.
        drop(timers);
        if let Some(((deadline, _), timer)) = expired {
            if let Some(timer) = timer.upgrade()
                && *timer.deadline.lock() == Some(deadline)
            {
                timer.readable.wake();
            }
        } else {
            let _ = timeout(next.map(|deadline| deadline.saturating_sub(now)), changed).await;
        }
    }
}

pub(crate) fn spawn_timerfd_task() {
    axtask::spawn_raw(
        || {
            // The runtime stores both the timeout and its cancellation on the same CPU.
            set_current_affinity(AxCpuMask::one_shot(axhal::percpu::this_cpu_id()));
            block_on(dispatch_timers());
        },
        "timerfd".to_owned(),
        axconfig::TASK_STACK_SIZE,
    );
}
