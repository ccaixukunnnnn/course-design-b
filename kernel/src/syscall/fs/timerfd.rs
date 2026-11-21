use core::time::Duration;

use axerrno::{AxError, AxResult};
use linux_raw_sys::general::{
    __kernel_itimerspec, __kernel_timespec, CLOCK_MONOTONIC, TFD_CLOEXEC, TFD_NONBLOCK,
    TFD_TIMER_ABSTIME,
};
use starry_vm::{VmMutPtr, VmPtr};

use crate::{
    file::{FileLike, timerfd::TimerFd},
    time::TimeValueLike,
};

pub fn sys_timerfd_create(clock: u32, flags: u32) -> AxResult<isize> {
    if clock != CLOCK_MONOTONIC || flags & !(TFD_CLOEXEC | TFD_NONBLOCK) != 0 {
        return Err(AxError::InvalidInput);
    }
    TimerFd::new(flags & TFD_NONBLOCK != 0)
        .add_to_fd_table(flags & TFD_CLOEXEC != 0)
        .map(|fd| fd as isize)
}

fn one_shot(value: Duration) -> __kernel_itimerspec {
    __kernel_itimerspec {
        it_interval: __kernel_timespec::from_time_value(Duration::ZERO),
        it_value: __kernel_timespec::from_time_value(value),
    }
}

pub fn sys_timerfd_settime(
    fd: i32,
    flags: u32,
    new_value: *const __kernel_itimerspec,
    old_value: *mut __kernel_itimerspec,
) -> AxResult<isize> {
    if flags & !TFD_TIMER_ABSTIME != 0 {
        return Err(AxError::InvalidInput);
    }
    let timer = TimerFd::from_fd(fd)?;
    // Both fields are timespecs containing only integer fields.
    let new_value = unsafe { new_value.vm_read_uninit()?.assume_init() };
    let interval = new_value.it_interval.try_into_time_value()?;
    let value = new_value.it_value.try_into_time_value()?;
    if !interval.is_zero() {
        return Err(AxError::Unsupported);
    }
    let old = timer.settime(value, flags & TFD_TIMER_ABSTIME != 0)?;
    if let Some(old_value) = old_value.nullable() {
        old_value.vm_write(one_shot(old))?;
    }
    Ok(0)
}

pub fn sys_timerfd_gettime(fd: i32, value: *mut __kernel_itimerspec) -> AxResult<isize> {
    let timer = TimerFd::from_fd(fd)?;
    value.vm_write(one_shot(timer.remaining()))?;
    Ok(0)
}
