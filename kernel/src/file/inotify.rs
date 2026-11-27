use alloc::{
    borrow::{Cow, ToOwned},
    collections::{BTreeMap, VecDeque},
    string::String,
    sync::{Arc, Weak},
    vec::Vec,
};
use core::{
    sync::atomic::{AtomicBool, AtomicU32, Ordering},
    task::Context,
};

use axerrno::{AxError, AxResult, LinuxError};
use axfs_ng_vfs::{Location, Mountpoint};
use axpoll::{IoEvents, PollSet, Pollable};
use axsync::Mutex;
use axtask::future::{block_on, poll_io};
use kspin::SpinNoPreempt;
use linux_raw_sys::general::{
    IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_IGNORED, IN_ISDIR, IN_MASK_ADD, IN_MASK_CREATE,
    IN_MOVE_SELF, IN_MOVED_FROM, IN_MOVED_TO, IN_Q_OVERFLOW, inotify_event,
};

use super::{FileLike, IoDst};

pub(crate) const WATCH_EVENTS: u32 =
    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
const MAX_WATCHES: usize = 128;
const MAX_EVENTS: usize = 128;
const HEADER_SIZE: usize = size_of::<inotify_event>();
// The VFS limits a component to 255 bytes, plus its trailing NUL.
const MAX_NAME_STORAGE: usize = 256;

type DirectoryKey = (u64, u64);
static WATCHES: SpinNoPreempt<BTreeMap<DirectoryKey, Vec<Subscription>>> =
    SpinNoPreempt::new(BTreeMap::new());
static RENAME_COOKIE: AtomicU32 = AtomicU32::new(1);

// Serializes watch changes with namespace operations in the syscall layer.
// Never take this mutex while holding FS_CONTEXT or a descriptor table lock.
pub(crate) static NOTIFY_NAMESPACE: Mutex<()> = Mutex::new(());

fn key(dir: &Location) -> DirectoryKey {
    (dir.mountpoint().device(), dir.inode())
}

struct Subscription {
    watcher: Weak<Inotify>,
    id: i32,
    mask: u32,
}

#[derive(PartialEq, Eq)]
struct Record {
    id: i32,
    mask: u32,
    cookie: u32,
    name: String,
}

impl Record {
    fn new(id: i32, mask: u32, cookie: u32, name: &str) -> Self {
        Self {
            id,
            mask,
            cookie,
            name: name.to_owned(),
        }
    }

    fn name_size(&self) -> usize {
        if self.name.is_empty() {
            0
        } else {
            (self.name.len() + 1).next_multiple_of(HEADER_SIZE)
        }
    }

    fn encode(&self, out: &mut [u8; HEADER_SIZE + MAX_NAME_STORAGE]) -> usize {
        let name_size = self.name_size();
        out[..4].copy_from_slice(&self.id.to_ne_bytes());
        out[4..8].copy_from_slice(&self.mask.to_ne_bytes());
        out[8..12].copy_from_slice(&self.cookie.to_ne_bytes());
        out[12..16].copy_from_slice(&(name_size as u32).to_ne_bytes());
        out[HEADER_SIZE..HEADER_SIZE + self.name.len()].copy_from_slice(self.name.as_bytes());
        HEADER_SIZE + name_size
    }
}

#[derive(Default)]
struct State {
    // Keep the mount identity alive without retaining a pre-rename directory entry.
    targets: BTreeMap<i32, (Arc<Mountpoint>, u64)>,
    next_id: i32,
    events: VecDeque<Record>,
}

pub(crate) struct Inotify {
    state: SpinNoPreempt<State>,
    nonblocking: AtomicBool,
    ready: PollSet,
}

impl Inotify {
    pub fn new(nonblocking: bool) -> Self {
        Self {
            state: SpinNoPreempt::new(State::default()),
            nonblocking: AtomicBool::new(nonblocking),
            ready: PollSet::new(),
        }
    }

    pub fn watch(self: &Arc<Self>, dir: Location, mask: u32) -> AxResult<i32> {
        let mut state = self.state.lock();
        let mut registry = WATCHES.lock();
        let directory = key(&dir);
        let events = mask & WATCH_EVENTS;
        if let Some(watch) = registry.get_mut(&directory).and_then(|entries| {
            entries
                .iter_mut()
                .find(|watch| watch.watcher.as_ptr() == Arc::as_ptr(self))
        }) {
            if mask & IN_MASK_CREATE != 0 {
                return Err(AxError::AlreadyExists);
            }
            if mask & IN_MASK_ADD != 0 {
                watch.mask |= events;
            } else {
                watch.mask = events;
            }
            return Ok(watch.id);
        }

        if state.targets.len() >= MAX_WATCHES {
            return Err(AxError::from(LinuxError::ENOSPC));
        }
        let id = state
            .next_id
            .checked_add(1)
            .ok_or(AxError::from(LinuxError::ENOSPC))?;
        state.next_id = id;
        state
            .targets
            .insert(id, (dir.mountpoint().clone(), dir.inode()));
        registry.entry(directory).or_default().push(Subscription {
            watcher: Arc::downgrade(self),
            id,
            mask: events,
        });
        Ok(id)
    }

    pub fn remove(&self, id: i32) -> AxResult<()> {
        let (mount, inode) = self
            .state
            .lock()
            .targets
            .remove(&id)
            .ok_or(AxError::InvalidInput)?;
        remove_subscription(&mut WATCHES.lock(), (mount.device(), inode), self);
        self.enqueue(Record::new(id, IN_IGNORED, 0, ""));
        Ok(())
    }

    fn enqueue(&self, record: Record) {
        let mut state = self.state.lock();
        let events = &mut state.events;
        if events.back() == Some(&record) {
            return;
        }
        // Reserve a slot for one overflow marker and drop new events until
        // the reader drains the backlog and observes that marker.
        if events
            .back()
            .is_some_and(|record| record.mask == IN_Q_OVERFLOW)
        {
            return;
        }
        if events.len() == MAX_EVENTS {
            events.push_back(Record::new(-1, IN_Q_OVERFLOW, 0, ""));
        } else {
            events.push_back(record);
        }
        drop(state);
        self.ready.wake();
    }

    fn take_record(&self, available: usize) -> AxResult<Option<Record>> {
        let mut state = self.state.lock();
        let Some(record) = state.events.front() else {
            return Ok(None);
        };
        if HEADER_SIZE + record.name_size() > available {
            return Err(AxError::InvalidInput);
        }
        Ok(state.events.pop_front())
    }
}

fn remove_subscription(
    registry: &mut BTreeMap<DirectoryKey, Vec<Subscription>>,
    directory: DirectoryKey,
    watcher: *const Inotify,
) {
    if let Some(entries) = registry.get_mut(&directory) {
        entries.retain(|entry| entry.watcher.as_ptr() != watcher);
        if entries.is_empty() {
            registry.remove(&directory);
        }
    }
}

impl Drop for Inotify {
    fn drop(&mut self) {
        let identity = self as *const Self;
        let mut registry = WATCHES.lock();
        for (mount, inode) in self.state.get_mut().targets.values() {
            remove_subscription(&mut registry, (mount.device(), *inode), identity);
        }
    }
}

impl FileLike for Inotify {
    fn read(&self, dst: &mut IoDst) -> AxResult<usize> {
        block_on(poll_io(self, IoEvents::IN, self.nonblocking(), || {
            let mut total = 0;
            loop {
                let record = match self.take_record(dst.remaining_mut()) {
                    Ok(Some(record)) => record,
                    Ok(None) if total == 0 => return Err(AxError::WouldBlock),
                    Err(error) if total == 0 => return Err(error),
                    _ => return Ok(total),
                };
                let mut buffer = [0; HEADER_SIZE + MAX_NAME_STORAGE];
                let len = record.encode(&mut buffer);
                // No queue lock may be held during a user-memory fault.
                match dst.write_all(&buffer[..len]) {
                    Ok(()) => total += len,
                    Err(error) if total == 0 => return Err(error),
                    Err(_) => return Ok(total),
                }
            }
        }))
    }

    fn nonblocking(&self) -> bool {
        self.nonblocking.load(Ordering::Acquire)
    }

    fn set_nonblocking(&self, nonblocking: bool) -> AxResult<()> {
        self.nonblocking.store(nonblocking, Ordering::Release);
        Ok(())
    }

    fn path(&self) -> Cow<'_, str> {
        "anon_inode:inotify".into()
    }
}

impl Pollable for Inotify {
    fn poll(&self) -> IoEvents {
        if self.state.lock().events.is_empty() {
            IoEvents::empty()
        } else {
            IoEvents::IN
        }
    }

    fn register(&self, cx: &mut Context<'_>, events: IoEvents) {
        if events.contains(IoEvents::IN) {
            self.ready.register(cx.waker());
        }
    }
}

fn publish(dir: &Location, mask: u32, cookie: u32, name: &str) {
    if name.len() >= MAX_NAME_STORAGE {
        return;
    }
    let watchers: Vec<_> = WATCHES
        .lock()
        .get(&key(dir))
        .into_iter()
        .flatten()
        .filter(|watch| watch.mask & mask != 0)
        .filter_map(|watch| watch.watcher.upgrade().map(|fd| (fd, watch.id)))
        .collect();
    // Releasing a last strong reference can run Drop, which uses WATCHES.
    for (watcher, id) in watchers {
        watcher.enqueue(Record::new(id, mask, cookie, name));
    }
}

pub(crate) fn created(dir: &Location, child: &Location) {
    publish(
        dir,
        IN_CREATE | if child.is_dir() { IN_ISDIR } else { 0 },
        0,
        child.name(),
    );
}

pub(crate) fn deleted(dir: &Location, child: &Location) {
    publish(
        dir,
        IN_DELETE | if child.is_dir() { IN_ISDIR } else { 0 },
        0,
        child.name(),
    );
    if child.is_dir() {
        invalidate_directory(child);
    }
}

pub(crate) fn invalidate_directory(dir: &Location) {
    let watchers = WATCHES.lock().remove(&key(dir));
    for subscription in watchers.into_iter().flatten() {
        if let Some(watcher) = subscription.watcher.upgrade() {
            watcher.state.lock().targets.remove(&subscription.id);
            if subscription.mask & IN_DELETE_SELF != 0 {
                watcher.enqueue(Record::new(subscription.id, IN_DELETE_SELF, 0, ""));
            }
            watcher.enqueue(Record::new(subscription.id, IN_IGNORED, 0, ""));
        }
    }
}

pub(crate) fn moved(from: (&Location, &str), to: (&Location, &str), child: &Location) {
    let mut cookie = RENAME_COOKIE.fetch_add(1, Ordering::Relaxed);
    if cookie == 0 {
        cookie = RENAME_COOKIE.fetch_add(1, Ordering::Relaxed);
    }
    let kind = if child.is_dir() { IN_ISDIR } else { 0 };
    publish(from.0, IN_MOVED_FROM | kind, cookie, from.1);
    publish(to.0, IN_MOVED_TO | kind, cookie, to.1);
    if child.is_dir() {
        publish(child, IN_MOVE_SELF, 0, "");
    }
}

pub(crate) fn same_object(left: &Location, right: &Location) -> bool {
    key(left) == key(right)
}
