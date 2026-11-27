use core::{
    future::Future,
    pin::Pin,
    task::{Context, Poll, Waker},
};

/// Poll once without allocating a task waker. A caller must still handle Pending.
pub(super) fn poll_once<F: Future>(future: Pin<&mut F>) -> Poll<F::Output> {
    future.poll(&mut Context::from_waker(Waker::noop()))
}

#[cfg(test)]
mod tests {
    use core::{future::poll_fn, pin::pin, task::Poll};

    use super::poll_once;

    #[test]
    fn ready_values_are_preserved() {
        let mut future = pin!(core::future::ready(42));
        assert_eq!(poll_once(future.as_mut()), Poll::Ready(42));
    }

    #[test]
    fn pending_future_can_be_polled_again() {
        let mut polls = 0;
        let mut future = pin!(poll_fn(|_| {
            polls += 1;
            if polls == 1 {
                Poll::Pending
            } else {
                Poll::Ready(7)
            }
        }));
        assert_eq!(poll_once(future.as_mut()), Poll::Pending);
        assert_eq!(poll_once(future.as_mut()), Poll::Ready(7));
    }
}
