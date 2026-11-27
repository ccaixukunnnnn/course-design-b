// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 KylinSoft Co., Ltd. <https://www.kylinos.cn/>
// Copyright (C) 2025 Azure-stars <Azure_stars@126.com>
// Copyright (C) 2025 Yuekai Jia <equation618@gmail.com>
// See LICENSES for license details.
//
// This file has been modified by KylinSoft on 2025.

use axerrno::{AxError, AxResult};
use axhal::paging::MappingFlags;
use axtask::current;
use memory_addr::{MemoryAddr, PAGE_SIZE_4K, VirtAddr};
use starry_vm::vm_write_slice;

use super::mincore_range::checked_page_count;
use crate::task::AsThread;

/// Check whether pages are resident in memory.
///
/// The mincore() system call determines whether pages of the calling process's
/// virtual memory are resident in RAM.
///
/// # Arguments
/// * `addr` - Starting address (must be a multiple of the page size)
/// * `length` - Length of the region in bytes (effectively rounded up to next page boundary)
/// * `vec` - Output array containing at least (length+PAGE_SIZE-1)/PAGE_SIZE bytes.
///
/// # Return Value
/// * `Ok(0)` on success
/// * `Err(EAGAIN)` - Kernel is temporarily out of resources (not implemented in StarryOS)
/// * `Err(EFAULT)` - vec points to an invalid address (handled by vm_write_slice)
/// * `Err(EINVAL)` - addr is not a multiple of the page size
/// * `Err(ENOMEM)` - length is greater than (TASK_SIZE - addr), or negative length, or `addr` to `addr`+`length` contained unmapped memory
///
/// # Notes from Linux man page
/// - The least significant bit (bit 0) is set if page is resident in memory
/// - Bits 1-7 are reserved and currently cleared
/// - Information is only a snapshot; pages can be swapped at any moment
///
/// # Linux Errors
/// - EAGAIN:  kernel temporarily out of resources
/// - EFAULT: vec points to invalid address
/// - EINVAL: addr not page-aligned
/// - ENOMEM: length > (TASK_SIZE - addr), negative length, or unmapped memory
pub fn sys_mincore(addr: usize, length: usize, vec: *mut u8) -> AxResult<isize> {
    let start_addr = VirtAddr::from(addr);

    // EINVAL: addr must be a multiple of the page size
    if !start_addr.is_aligned(PAGE_SIZE_4K) {
        return Err(AxError::InvalidInput);
    }

    debug!("sys_mincore <= addr: {addr:#x}, length: {length:#x}, vec: {vec:?}");

    // Special case: length=0
    // According to Linux kernel (mm/mincore.c), length=0 returns success
    // WITHOUT validating that addr is mapped.  This is intentional behavior
    // to match POSIX semantics where a zero-length operation is a no-op.
    if length == 0 {
        return Ok(0);
    }

    if vec.is_null() {
        return Err(AxError::BadAddress);
    }

    let curr = current();
    let proc_data = &curr.as_thread().proc_data;
    let page_count = {
        let aspace = proc_data.aspace.lock();
        checked_page_count(
            addr,
            length,
            PAGE_SIZE_4K,
            aspace.base().as_usize(),
            aspace.end().as_usize(),
        )
        .ok_or(AxError::NoMemory)?
    };

    // Bound kernel memory use, and never hold the address-space lock across a
    // user copy: the copy itself can fault and need the same lock.
    let mut result = [0u8; 256];
    let mut done = 0usize;
    while done < page_count {
        let count = (page_count - done).min(result.len());
        {
            let aspace = proc_data.aspace.lock();
            for (index, slot) in result[..count].iter_mut().enumerate() {
                let page = VirtAddr::from(addr + (done + index) * PAGE_SIZE_4K);
                let area = aspace.find_area(page).ok_or(AxError::NoMemory)?;
                if !area.flags().contains(MappingFlags::USER) {
                    return Err(AxError::NoMemory);
                }
                *slot = u8::from(aspace.page_table().query(page).is_ok());
            }
        }
        vm_write_slice(vec.wrapping_add(done), &result[..count])?;
        done += count;
    }

    Ok(0)
}
