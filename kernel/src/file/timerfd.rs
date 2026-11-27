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

#[derive(Default)]
struct TimerState {
    // Earliest expiration that has not been read yet.
    deadline: Option<Duration>,
    interval: Duration,
}

impl TimerState {
    fn remaining(&self, now: Duration) -> Duration {
        match self.deadline {
            Some(deadline) if deadline <= now && !self.interval.is_zero() => {
                periodic_progress(now - deadline, self.interval).1
            }
            Some(deadline) => deadline.saturating_sub(now),
            None => Duration::ZERO,
        }
    }
}

fn periodic_progress(elapsed: Duration, interval: Duration) -> (u64, Duration) {
    let period = interval.as_nanos();
    let elapsed = elapsed.as_nanos();
    let expirations = u64::try_from(1 + elapsed / period).unwrap_or(u64::MAX);
    let until_next = period - elapsed % period;
    let seconds = until_next / 1_000_000_000;
    let nanos = until_next % 1_000_000_000;
    (expirations, Duration::new(seconds as u64, nanos as u32))
}

pub struct TimerFd {
    state: SpinNoPreempt<TimerState>,
    weak_self: Weak<Self>,
    nonblocking: AtomicBool,
    readable: PollSet,
}

impl TimerFd {
    pub fn new(nonblocking: bool) -> Arc<Self> {
        Arc::new_cyclic(|weak| Self {
            state: SpinNoPreempt::new(TimerState::default()),
            weak_self: weak.clone(),
            nonblocking: AtomicBool::new(nonblocking),
            readable: PollSet::new(),
        })
    }

    fn key(&self, deadline: Duration) -> TimerKey {
        (deadline, self as *const Self as usize)
    }

    pub fn settime(
        &self,
        value: Duration,
        interval: Duration,
        absolute: bool,
    ) -> AxResult<(Duration, Duration)> {
        let mut current = self.state.lock();
        let now = monotonic_time();
        let deadline = if value.is_zero() {
            None
        } else if absolute {
            Some(value)
        } else {
            Some(now.checked_add(value).ok_or(AxError::InvalidInput)?)
        };
        let mut timers = TIMERS.lock();
        if let Some(old) = current.deadline {
            timers.remove(&self.key(old));
        }
        if let Some(next) = deadline.filter(|deadline| *deadline > now) {
            timers.insert(self.key(next), self.weak_self.clone());
        }
        let previous = core::mem::replace(&mut *current, TimerState { deadline, interval });
        drop(timers);
        drop(current);
        if deadline.is_some_and(|deadline| deadline <= now) {
            self.readable.wake();
        }
        SCHEDULE_CHANGED.notify(1);
        Ok((previous.remaining(now), previous.interval))
    }

    pub fn gettime(&self) -> (Duration, Duration) {
        let current = self.state.lock();
        (current.remaining(monotonic_time()), current.interval)
    }

    fn expired(&self) -> bool {
        self.state
            .lock()
            .deadline
            .is_some_and(|deadline| deadline <= monotonic_time())
    }
}

impl Drop for TimerFd {
    fn drop(&mut self) {
        if let Some(deadline) = self.state.get_mut().deadline {
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
            let mut current = self.state.lock();
            let now = monotonic_time();
            let deadline = current
                .deadline
                .filter(|deadline| *deadline <= now)
                .ok_or(AxError::WouldBlock)?;
            let (expirations, next) = if current.interval.is_zero() {
                (1, None)
            } else {
                let (expirations, until_next) = periodic_progress(now - deadline, current.interval);
                (expirations, now.checked_add(until_next))
            };
            let mut timers = TIMERS.lock();
            timers.remove(&self.key(deadline));
            if let Some(next) = next {
                timers.insert(self.key(next), self.weak_self.clone());
            }
            current.deadline = next;
            drop(timers);
            // A user copy may fault and enter the scheduler.
            drop(current);
            if next.is_some() {
                SCHEDULE_CHANGED.notify(1);
            }
            dst.write(&expirations.to_ne_bytes())?;
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
                && timer.state.lock().deadline == Some(deadline)
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
