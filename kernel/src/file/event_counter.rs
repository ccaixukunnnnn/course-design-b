use core::sync::atomic::{AtomicU64, Ordering};

/// Atomically consume one semaphore token, or the entire ordinary counter.
pub(super) fn take(counter: &AtomicU64, semaphore: bool) -> Option<u64> {
    counter
        .fetch_update(Ordering::Release, Ordering::Acquire, |count| {
            if count == 0 {
                None
            } else if semaphore {
                Some(count - 1)
            } else {
                Some(0)
            }
        })
        .ok()
        .map(|old| if semaphore { 1 } else { old })
}

#[cfg(test)]
mod tests {
    extern crate std;

    use std::{sync::Arc, thread};

    use super::*;

    #[test]
    fn semaphore_returns_one_for_every_token() {
        let counter = AtomicU64::new(3);
        for expected_remaining in (0..3).rev() {
            assert_eq!(take(&counter, true), Some(1));
            assert_eq!(counter.load(Ordering::Relaxed), expected_remaining);
        }
        assert_eq!(take(&counter, true), None);
    }

    #[test]
    fn ordinary_read_drains_counter() {
        let counter = AtomicU64::new(42);
        assert_eq!(take(&counter, false), Some(42));
        assert_eq!(take(&counter, false), None);
    }

    #[test]
    fn concurrent_readers_cannot_consume_a_token_twice() {
        let counter = Arc::new(AtomicU64::new(1000));
        let workers: Vec<_> = (0..4)
            .map(|_| {
                let counter = counter.clone();
                thread::spawn(move || {
                    let mut consumed = 0;
                    while let Some(value) = take(&counter, true) {
                        assert_eq!(value, 1);
                        consumed += 1;
                    }
                    consumed
                })
            })
            .collect();
        assert_eq!(
            workers.into_iter().map(|w| w.join().unwrap()).sum::<u64>(),
            1000
        );
        assert_eq!(counter.load(Ordering::Relaxed), 0);
    }
}
