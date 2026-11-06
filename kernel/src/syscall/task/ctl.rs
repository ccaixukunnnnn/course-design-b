use core::ffi::c_char;

use axerrno::{AxError, AxResult};
use axtask::current;
use linux_raw_sys::general::{__user_cap_data_struct, __user_cap_header_struct};
use starry_vm::vm_write_slice;

use crate::{mm::vm_load_string, task::AsThread};

pub fn sys_capget(
    _header: *mut __user_cap_header_struct,
    _data: *mut __user_cap_data_struct,
) -> AxResult<isize> {
    Err(AxError::Unsupported)
}

pub fn sys_capset(
    _header: *mut __user_cap_header_struct,
    _data: *mut __user_cap_data_struct,
) -> AxResult<isize> {
    Err(AxError::Unsupported)
}

pub fn sys_umask(mask: u32) -> AxResult<isize> {
    let curr = current();
    let old = curr.as_thread().proc_data.replace_umask(mask);
    Ok(old as isize)
}

fn unchanged_root(ids: &[u32]) -> AxResult<isize> {
    if ids.iter().all(|&id| id == 0 || id == u32::MAX) {
        Ok(0)
    } else {
        Err(AxError::Unsupported)
    }
}

pub fn sys_setreuid(ruid: u32, euid: u32) -> AxResult<isize> {
    unchanged_root(&[ruid, euid])
}

pub fn sys_setresuid(ruid: u32, euid: u32, suid: u32) -> AxResult<isize> {
    unchanged_root(&[ruid, euid, suid])
}

pub fn sys_setresgid(rgid: u32, egid: u32, sgid: u32) -> AxResult<isize> {
    unchanged_root(&[rgid, egid, sgid])
}

pub fn sys_get_mempolicy(
    _policy: *mut i32,
    _nodemask: *mut usize,
    _maxnode: usize,
    _addr: usize,
    _flags: usize,
) -> AxResult<isize> {
    warn!("Dummy get_mempolicy called");
    Ok(0)
}

/// prctl() is called with a first argument describing what to do, and further
/// arguments with a significance depending on the first one.
/// The first argument can be:
/// - PR_SET_NAME: set the name of the calling thread, using the value pointed to by `arg2`
/// - PR_GET_NAME: get the name of the calling
/// - PR_SET_SECCOMP: enable seccomp mode, with the mode specified in `arg2`
/// - PR_MCE_KILL: set the machine check exception policy
/// - PR_SET_MM options: set various memory management options (start/end code/data/brk/stack)
pub fn sys_prctl(
    option: u32,
    arg2: usize,
    arg3: usize,
    arg4: usize,
    arg5: usize,
) -> AxResult<isize> {
    use linux_raw_sys::prctl::*;

    debug!("sys_prctl <= option: {option}, args: {arg2}, {arg3}, {arg4}, {arg5}");

    match option {
        PR_SET_NAME => {
            let s = vm_load_string(arg2 as *const c_char)?;
            current().set_name(&s);
        }
        PR_GET_NAME => {
            let name = current().name();
            let len = name.len().min(15);
            let mut buf = [0; 16];
            buf[..len].copy_from_slice(&name.as_bytes()[..len]);
            vm_write_slice(arg2 as _, &buf)?;
        }
        // Seccomp is not implemented; accepting this request would create a
        // dangerous false impression that the calling process is sandboxed.
        PR_SET_SECCOMP => return Err(AxError::Unsupported),
        PR_MCE_KILL => {}
        PR_SET_MM => {
            // not implemented; but avoid annoying warnings
            return Err(AxError::InvalidInput);
        }
        _ => {
            warn!("sys_prctl: unsupported option {option}");
            return Err(AxError::InvalidInput);
        }
    }

    Ok(0)
}
