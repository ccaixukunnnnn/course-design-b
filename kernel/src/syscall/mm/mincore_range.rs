/// Validate a nonempty, page-aligned address range and return its page count.
pub(super) fn checked_page_count(
    start: usize,
    length: usize,
    page_size: usize,
    base: usize,
    end: usize,
) -> Option<usize> {
    if length == 0 || page_size == 0 || !start.is_multiple_of(page_size) || start < base {
        return None;
    }
    let pages = length.checked_add(page_size - 1)? / page_size;
    let rounded = pages.checked_mul(page_size)?;
    if start.checked_add(rounded)? > end {
        return None;
    }
    Some(pages)
}

#[cfg(test)]
mod tests {
    use super::checked_page_count;

    #[test]
    fn validates_and_rounds_ranges() {
        assert_eq!(checked_page_count(0x1000, 1, 4096, 0x1000, 0x5000), Some(1));
        assert_eq!(
            checked_page_count(0x1000, 4097, 4096, 0x1000, 0x5000),
            Some(2)
        );
        assert_eq!(
            checked_page_count(0x4000, 4096, 4096, 0x1000, 0x5000),
            Some(1)
        );
        assert_eq!(checked_page_count(0x4000, 4097, 4096, 0x1000, 0x5000), None);
    }

    #[test]
    fn rejects_overflow_and_invalid_ranges() {
        assert_eq!(
            checked_page_count(0x1000, usize::MAX, 4096, 0x1000, usize::MAX),
            None
        );
        assert_eq!(checked_page_count(0, 4096, 4096, 0x1000, 0x5000), None);
        assert_eq!(checked_page_count(0x1001, 1, 4096, 0x1000, 0x5000), None);
        assert_eq!(checked_page_count(0x1000, 0, 4096, 0x1000, 0x5000), None);
        assert_eq!(checked_page_count(0x1000, 1, 0, 0x1000, 0x5000), None);
    }
}
