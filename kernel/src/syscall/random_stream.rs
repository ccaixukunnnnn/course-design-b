/// Copy from a random source in bounded chunks. Preserve a short successful
/// result if a later read or user copy fails, as with other streaming I/O.
pub(super) fn copy_bounded<E>(
    length: usize,
    mut read: impl FnMut(&mut [u8]) -> Result<usize, E>,
    mut write: impl FnMut(usize, &[u8]) -> Result<(), E>,
    invalid_count: E,
) -> Result<usize, E> {
    let mut bytes = [0u8; 256];
    let mut written = 0;
    while written < length {
        let request = (length - written).min(bytes.len());
        let count = match read(&mut bytes[..request]) {
            Ok(n) if n <= request => n,
            Ok(_) => {
                return if written == 0 {
                    Err(invalid_count)
                } else {
                    Ok(written)
                };
            }
            Err(e) => return if written == 0 { Err(e) } else { Ok(written) },
        };
        if count == 0 {
            break;
        }
        if let Err(e) = write(written, &bytes[..count]) {
            return if written == 0 { Err(e) } else { Ok(written) };
        }
        written += count;
        if count < request {
            break;
        }
    }
    Ok(written)
}

#[cfg(test)]
mod tests {
    extern crate std;
    use std::{cell::RefCell, vec::Vec};

    use super::copy_bounded;

    #[test]
    fn chunks_large_requests_and_preserves_data() {
        let requests = RefCell::new(Vec::new());
        let output = RefCell::new(Vec::new());
        let result = copy_bounded(
            513,
            |buf| {
                requests.borrow_mut().push(buf.len());
                buf.fill(0xa5);
                Ok::<_, u8>(buf.len())
            },
            |offset, buf| {
                assert_eq!(offset, output.borrow().len());
                output.borrow_mut().extend_from_slice(buf);
                Ok(())
            },
            9,
        );
        assert_eq!(result, Ok(513));
        assert_eq!(*requests.borrow(), [256, 256, 1]);
        assert_eq!(*output.borrow(), std::vec![0xa5; 513]);
    }

    #[test]
    fn errors_and_short_results_are_bounded() {
        assert_eq!(
            copy_bounded(usize::MAX, |_| Err::<usize, _>(7), |_, _| Ok(()), 9),
            Err(7)
        );
        assert_eq!(
            copy_bounded(8, |buf| Ok::<_, u8>(buf.len() + 1), |_, _| Ok(()), 9),
            Err(9)
        );
        assert_eq!(
            copy_bounded(
                8,
                |_| Ok::<_, u8>(3),
                |_, buf| {
                    assert_eq!(buf.len(), 3);
                    Ok(())
                },
                9
            ),
            Ok(3)
        );
        let mut calls = 0;
        let result = copy_bounded(
            513,
            |buf| Ok::<_, u8>(buf.len()),
            |_, _| {
                calls += 1;
                if calls == 2 { Err(8) } else { Ok(()) }
            },
            9,
        );
        assert_eq!(result, Ok(256));
    }
}
