use core::ffi::c_char;

use axerrno::{AxError, AxResult};
use linux_raw_sys::general::{
    AT_FDCWD, IN_CLOEXEC, IN_DONT_FOLLOW, IN_MASK_ADD, IN_MASK_CREATE, IN_NONBLOCK, IN_ONLYDIR,
};

use crate::{
    file::{
        FileLike,
        inotify::{Inotify, NOTIFY_NAMESPACE, WATCH_EVENTS},
        with_fs,
    },
    mm::vm_load_string,
};

pub fn sys_inotify_init1(flags: u32) -> AxResult<isize> {
    if flags & !(IN_NONBLOCK | IN_CLOEXEC) != 0 {
        return Err(AxError::InvalidInput);
    }
    Inotify::new(flags & IN_NONBLOCK != 0)
        .add_to_fd_table(flags & IN_CLOEXEC != 0)
        .map(|fd| fd as isize)
}

pub fn sys_inotify_add_watch(fd: i32, path: *const c_char, mask: u32) -> AxResult<isize> {
    const FLAGS: u32 = IN_ONLYDIR | IN_DONT_FOLLOW | IN_MASK_ADD | IN_MASK_CREATE;
    if mask & WATCH_EVENTS == 0
        || mask & !(WATCH_EVENTS | FLAGS) != 0
        || mask & (IN_MASK_ADD | IN_MASK_CREATE) == (IN_MASK_ADD | IN_MASK_CREATE)
    {
        return Err(AxError::InvalidInput);
    }
    let instance = Inotify::from_fd(fd)?;
    let path = vm_load_string(path)?;
    let _namespace = NOTIFY_NAMESPACE.lock();
    let dir = with_fs(AT_FDCWD, |fs| {
        let entry = if mask & IN_DONT_FOLLOW == 0 {
            fs.resolve(path.as_str())?
        } else {
            fs.resolve_no_follow(path.as_str())?
        };
        entry.check_is_dir()?;
        Ok(entry)
    })?;
    instance.watch(dir, mask).map(|id| id as isize)
}

pub fn sys_inotify_rm_watch(fd: i32, id: i32) -> AxResult<isize> {
    let instance = Inotify::from_fd(fd)?;
    let _namespace = NOTIFY_NAMESPACE.lock();
    instance.remove(id)?;
    Ok(0)
}
