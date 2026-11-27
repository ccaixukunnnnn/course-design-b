use core::ffi::c_char;

use axconfig::ARCH;
use axerrno::{AxError, AxResult};
use axfs::FS_CONTEXT;
use linux_raw_sys::{
    general::{GRND_INSECURE, GRND_NONBLOCK, GRND_RANDOM},
    system::{new_utsname, sysinfo},
};
use starry_vm::{VmMutPtr, vm_write_slice};

use super::random_stream::copy_bounded;
use crate::task::processes;

pub fn sys_getuid() -> AxResult<isize> {
    Ok(0)
}

pub fn sys_geteuid() -> AxResult<isize> {
    Ok(0)
}

pub fn sys_getgid() -> AxResult<isize> {
    Ok(0)
}

pub fn sys_getegid() -> AxResult<isize> {
    Ok(0)
}

pub fn sys_setuid(uid: u32) -> AxResult<isize> {
    match uid {
        0 => Ok(0),
        u32::MAX => Err(AxError::InvalidInput),
        _ => Err(AxError::Unsupported),
    }
}

pub fn sys_setgid(gid: u32) -> AxResult<isize> {
    match gid {
        0 => Ok(0),
        u32::MAX => Err(AxError::InvalidInput),
        _ => Err(AxError::Unsupported),
    }
}

pub fn sys_getgroups(size: usize, list: *mut u32) -> AxResult<isize> {
    debug!("sys_getgroups <= size: {size}");
    if size < 1 {
        return Err(AxError::InvalidInput);
    }
    vm_write_slice(list, &[0])?;
    Ok(1)
}

pub fn sys_setgroups(_size: usize, _list: *const u32) -> AxResult<isize> {
    Err(AxError::Unsupported)
}

const fn pad_str(info: &str) -> [c_char; 65] {
    let mut data: [c_char; 65] = [0; 65];
    // this needs #![feature(const_copy_from_slice)]
    // data[..info.len()].copy_from_slice(info.as_bytes());
    unsafe {
        core::ptr::copy_nonoverlapping(info.as_ptr().cast(), data.as_mut_ptr(), info.len());
    }
    data
}

const UTSNAME: new_utsname = new_utsname {
    sysname: pad_str("Linux"),
    nodename: pad_str("starry"),
    release: pad_str("10.0.0"),
    version: pad_str("10.0.0"),
    machine: pad_str(ARCH),
    domainname: pad_str("https://github.com/Starry-OS/StarryOS"),
};

pub fn sys_uname(name: *mut new_utsname) -> AxResult<isize> {
    name.vm_write(UTSNAME)?;
    Ok(0)
}

pub fn sys_sysinfo(info: *mut sysinfo) -> AxResult<isize> {
    // FIXME: Zeroable
    let mut kinfo: sysinfo = unsafe { core::mem::zeroed() };
    kinfo.procs = processes().len() as _;
    kinfo.mem_unit = 1;
    info.vm_write(kinfo)?;
    Ok(0)
}

/// Report the current CPU and the only supported NUMA node as a snapshot.
pub fn sys_getcpu(cpu: *mut u32, node: *mut u32) -> AxResult<isize> {
    let cpu_id = axhal::percpu::this_cpu_id() as u32;
    if !cpu.is_null() {
        cpu.vm_write(cpu_id)?;
    }
    if !node.is_null() {
        node.vm_write(0)?;
    }
    Ok(0)
}

pub fn sys_syslog(_type: i32, _buf: *mut c_char, _len: usize) -> AxResult<isize> {
    Ok(0)
}

bitflags::bitflags! {
    #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
    pub struct GetRandomFlags: u32 {
        const NONBLOCK = GRND_NONBLOCK;
        const RANDOM = GRND_RANDOM;
        const INSECURE = GRND_INSECURE;
    }
}

pub fn sys_getrandom(buf: *mut u8, len: usize, flags: u32) -> AxResult<isize> {
    let flags = GetRandomFlags::from_bits(flags).ok_or(AxError::InvalidInput)?;
    if flags.contains(GetRandomFlags::RANDOM | GetRandomFlags::INSECURE) {
        return Err(AxError::InvalidInput);
    }
    if len == 0 {
        return Ok(0);
    }

    debug!("sys_getrandom <= buf: {buf:p}, len: {len}, flags: {flags:?}");

    let path = if flags.contains(GetRandomFlags::RANDOM) {
        "/dev/random"
    } else {
        "/dev/urandom"
    };

    let f = FS_CONTEXT.lock().resolve(path)?;
    let file = f.entry().as_file()?;
    let limit = len.min(isize::MAX as usize);
    copy_bounded(
        limit,
        |bytes| file.read_at(bytes, 0),
        |offset, bytes| vm_write_slice(buf.wrapping_add(offset), bytes).map_err(Into::into),
        AxError::Io,
    )
    .map(|written| written as _)
}

pub fn sys_seccomp(_op: u32, _flags: u32, _args: *const ()) -> AxResult<isize> {
    // Never report success for a filter that was not installed.
    Err(AxError::Unsupported)
}

#[cfg(target_arch = "riscv64")]
pub fn sys_riscv_flush_icache() -> AxResult<isize> {
    riscv::asm::fence_i();
    Ok(0)
}
