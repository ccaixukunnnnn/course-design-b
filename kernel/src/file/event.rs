use alloc::{borrow::Cow, sync::Arc};
use core::{
    future::Future,
    pin::pin,
    sync::atomic::{AtomicBool, AtomicU64, Ordering},
    task::{Context, Poll},
};

use axerrno::AxError;
use axpoll::{IoEvents, PollSet, Pollable};
use axtask::future::{block_on, poll_io};

use crate::file::{FileLike, IoDst, IoSrc, event_counter::take, event_poll::poll_once};

fn run_io<F: Future<Output = axio::Result<usize>>>(
    future: F,
    nonblocking: bool,
) -> axio::Result<usize> {
    if !nonblocking {
        return block_on(future);
    }
    let mut future = pin!(future);
    match poll_once(future.as_mut()) {
        Poll::Ready(result) => result,
        // Preserve correctness if the underlying poll implementation ever
        // returns Pending for a nonblocking operation.
        Poll::Pending => block_on(future),
    }
}

pub struct EventFd {
    count: AtomicU64,
    semaphore: bool,
    non_blocking: AtomicBool,

    poll_rx: PollSet,
    poll_tx: PollSet,
}

impl EventFd {
    pub fn new(initval: u64, semaphore: bool) -> Arc<Self> {
        Arc::new(Self {
            count: AtomicU64::new(initval),
            semaphore,
            non_blocking: AtomicBool::new(false),

            poll_rx: PollSet::new(),
            poll_tx: PollSet::new(),
        })
    }
}

impl FileLike for EventFd {
    fn read(&self, dst: &mut IoDst) -> axio::Result<usize> {
        if dst.remaining_mut() < size_of::<u64>() {
            return Err(AxError::InvalidInput);
        }

        let nonblocking = self.nonblocking();
        run_io(
            poll_io(self, IoEvents::IN, nonblocking, || {
                match take(&self.count, self.semaphore) {
                    Some(value) => {
                        // The count has already changed even if copying to
                        // userspace fails; wake writers before that can happen.
                        self.poll_tx.wake();
                        dst.write(&value.to_ne_bytes())?;
                        Ok(size_of::<u64>())
                    }
                    None => Err(AxError::WouldBlock),
                }
            }),
            nonblocking,
        )
    }

    fn write(&self, src: &mut IoSrc) -> axio::Result<usize> {
        if src.remaining() < size_of::<u64>() {
            return Err(AxError::InvalidInput);
        }

        let mut value = [0; size_of::<u64>()];
        src.read(&mut value)?;
        let value = u64::from_ne_bytes(value);
        if value == u64::MAX {
            return Err(AxError::InvalidInput);
        }

        let nonblocking = self.nonblocking();
        run_io(
            poll_io(self, IoEvents::OUT, nonblocking, || {
                let result =
                    self.count
                        .fetch_update(Ordering::Release, Ordering::Acquire, |count| {
                            if u64::MAX - count > value {
                                Some(count + value)
                            } else {
                                None
                            }
                        });
                match result {
                    Ok(_) => {
                        self.poll_rx.wake();
                        Ok(size_of::<u64>())
                    }
                    Err(_) => Err(AxError::WouldBlock),
                }
            }),
            nonblocking,
        )
    }

    fn nonblocking(&self) -> bool {
        self.non_blocking.load(Ordering::Acquire)
    }

    fn set_nonblocking(&self, non_blocking: bool) -> axio::Result {
        self.non_blocking.store(non_blocking, Ordering::Release);
        Ok(())
    }

    fn path(&self) -> Cow<'_, str> {
        "anon_inode:[eventfd]".into()
    }
}

impl Pollable for EventFd {
    fn poll(&self) -> IoEvents {
        let mut events = IoEvents::empty();
        let count = self.count.load(Ordering::Acquire);
        events.set(IoEvents::IN, count > 0);
        events.set(IoEvents::OUT, u64::MAX - 1 > count);
        events
    }

    fn register(&self, context: &mut Context<'_>, events: IoEvents) {
        if events.contains(IoEvents::IN) {
            self.poll_rx.register(context.waker());
        }
        if events.contains(IoEvents::OUT) {
            self.poll_tx.register(context.waker());
        }
    }
}
