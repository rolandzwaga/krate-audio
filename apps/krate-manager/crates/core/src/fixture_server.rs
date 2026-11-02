//! A loopback HTTP fixture server for tests (FR-070, FR-086; plan §4.7). Compiled only under
//! `cfg(any(test, feature = "test-support"))`.
//!
//! `std` only. Binds `127.0.0.1:0` or `[::1]:0`, one thread per connection, every read bounded (64 KiB request head,
//! 16 KiB TLS record) and polled against the shutdown flag, every wait (`Stall`, `Delayed`, `HeadersThenStall`) on one
//! `Condvar` that `Drop` signals (a `Held` reply re-checks the shutdown flag every `POLL`). It opens no outbound socket:
//! the only dial is `Drop`'s wake-up of its own listener.

use std::collections::BTreeMap;
use std::fmt;
use std::io::{ErrorKind, Read, Write};
use std::net::{Shutdown, SocketAddr, TcpListener, TcpStream};
use std::sync::{Arc, Condvar, Mutex, MutexGuard, PoisonError};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

/// One scripted answer.
#[derive(Clone, Debug)]
pub enum Reply {
    Ok {
        body: Vec<u8>,
        content_type: &'static str,
    },
    Status(u16),
    Redirect {
        status: u16,
        location: String,
    },
    Truncated {
        declared_len: usize,
        sent: Vec<u8>,
    },
    Stall(Duration),
    Close,
    Garbage(Vec<u8>),
    Gzip {
        body: Vec<u8>,
    },
    ConnectOk,
    ConnectRefused(u16),
    /// 200 with `Content-Length: declared_len`, then `sent`, then wait `stall` (ends on Drop), then close (SI-22).
    HeadersThenStall {
        declared_len: usize,
        sent: Vec<u8>,
        stall: Duration,
    },
    /// Wait `after` (ends on Drop), then answer with `reply` (SI-22).
    Delayed {
        after: Duration,
        reply: Box<Reply>,
    },
    /// 206 for a `Range: bytes=<n>-` with `n` below the body length, 416 at or past it, else 200 with the whole
    /// body (Phase 7 FR-084).
    Ranged {
        body: Vec<u8>,
    },
    /// The k-th request to the route gets the k-th reply; the last repeats. Resolved only at the top level.
    Script(Vec<Reply>),
    /// 200 with Content-Length, `body` sent `chunk` bytes at a time with `interval` between chunks.
    Paced {
        body: Vec<u8>,
        chunk: usize,
        interval: Duration,
    },
    /// Phase 12 PD-21: held (listed by `gate.open()`) until the test releases it, then answered with `reply`; a hold
    /// still waiting when the server drops ends unanswered.
    Held {
        gate: HoldGate,
        reply: Box<Reply>,
    },
}

/// Phase 12 PD-21: the test's handle on `Reply::Held` answers. Clones share one set of holds.
#[derive(Clone, Default)]
pub struct HoldGate(Arc<HoldState>);

#[derive(Default)]
struct HoldState {
    holds: Mutex<Holds>,
    changed: Condvar,
}

#[derive(Default)]
struct Holds {
    next: u64,
    /// The held requests, oldest first: (ticket, path).
    open: Vec<(u64, String)>,
}

impl HoldGate {
    pub fn new() -> HoldGate {
        HoldGate::default()
    }

    /// The paths whose response is held, oldest first.
    pub fn open(&self) -> Vec<String> {
        lock(&self.0.holds)
            .open
            .iter()
            .map(|(_, path)| path.clone())
            .collect()
    }

    /// Waits until at least `n` responses are held or `timeout` passes; `true` when `n` are held.
    pub fn wait_open(&self, n: usize, timeout: Duration) -> bool {
        let guard = lock(&self.0.holds);
        let (guard, _) = self
            .0
            .changed
            .wait_timeout_while(guard, timeout, |h| h.open.len() < n)
            .unwrap_or_else(PoisonError::into_inner);
        guard.open.len() >= n
    }

    /// Releases the oldest held response; its path, or `None` when nothing is held.
    pub fn release_one(&self) -> Option<String> {
        let mut holds = lock(&self.0.holds);
        if holds.open.is_empty() {
            return None;
        }
        let (_, path) = holds.open.remove(0);
        drop(holds);
        self.0.changed.notify_all();
        Some(path)
    }

    /// Releases every held response.
    pub fn release_all(&self) {
        lock(&self.0.holds).open.clear();
        self.0.changed.notify_all();
    }

    /// Records `path` as held; the ticket `wait_release` waits on.
    fn enter(&self, path: &str) -> u64 {
        let mut holds = lock(&self.0.holds);
        let ticket = holds.next;
        holds.next += 1;
        holds.open.push((ticket, path.to_owned()));
        drop(holds);
        self.0.changed.notify_all();
        ticket
    }

    /// Waits in `POLL` slices until `ticket` is released (`true`) or the server stops (`false`, the hold withdrawn).
    fn wait_release(&self, ticket: u64, shared: &Shared) -> bool {
        let mut holds = lock(&self.0.holds);
        loop {
            if !holds.open.iter().any(|(t, _)| *t == ticket) {
                return true;
            }
            if shared.stopping() {
                holds.open.retain(|(t, _)| *t != ticket);
                drop(holds);
                self.0.changed.notify_all();
                return false;
            }
            holds = self
                .0
                .changed
                .wait_timeout(holds, POLL)
                .unwrap_or_else(PoisonError::into_inner)
                .0;
        }
    }
}

impl fmt::Debug for HoldGate {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("HoldGate")
            .field("open", &self.open())
            .finish()
    }
}

/// One request as the server read it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RecordedRequest {
    pub method: String,
    pub path: String,
    pub headers: Vec<(String, String)>,
}

/// The request head bound (CRLFCRLF must arrive within it).
const MAX_HEAD: usize = 64 * 1024;
/// The TLS record body bound.
const MAX_TLS_RECORD: usize = 16 * 1024;
/// How often a blocked read re-checks the shutdown flag.
const POLL: Duration = Duration::from_millis(20);
/// A write that cannot progress for this long is abandoned.
const WRITE_TIMEOUT: Duration = Duration::from_secs(2);
/// After the TLS answer: read until EOF or this long (FR-070).
const TLS_DRAIN: Duration = Duration::from_secs(2);
const TLS_ANSWER: &[u8] =
    b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

struct Shared {
    routes: Mutex<Vec<(String, Reply)>>,
    requests: Mutex<Vec<RecordedRequest>>,
    /// Per path: how many requests a `Script` route has resolved.
    hits: Mutex<BTreeMap<String, usize>>,
    stop: Mutex<bool>,
    wake: Condvar,
    workers: Mutex<Vec<JoinHandle<()>>>,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

impl Shared {
    fn stopping(&self) -> bool {
        *lock(&self.stop)
    }

    /// Waits up to `d` or until shutdown; `true` when the server is shutting down.
    fn wait(&self, d: Duration) -> bool {
        let guard = lock(&self.stop);
        let (guard, _) = self
            .wake
            .wait_timeout_while(guard, d, |stop| !*stop)
            .unwrap_or_else(PoisonError::into_inner);
        *guard
    }

    /// The route's reply; a `Script` resolves to its k-th reply for the route's k-th request (the last repeats).
    fn reply_for(&self, path: &str) -> Reply {
        let reply = lock(&self.routes)
            .iter()
            .find(|(p, _)| p == path)
            .map_or(Reply::Status(404), |(_, r)| r.clone());
        let Reply::Script(replies) = reply else {
            return reply;
        };
        let mut hits = lock(&self.hits);
        let hit = hits.entry(path.to_owned()).or_insert(0);
        let k = *hit;
        *hit += 1;
        replies
            .get(k)
            .or_else(|| replies.last())
            .cloned()
            .unwrap_or(Reply::Status(404))
    }
}

pub struct FixtureServer {
    addr: SocketAddr,
    shared: Arc<Shared>,
    accept: Option<JoinHandle<()>>,
}

impl FixtureServer {
    /// Binds `127.0.0.1:0` only.
    pub fn start(routes: Vec<(String, Reply)>) -> FixtureServer {
        let listener = TcpListener::bind("127.0.0.1:0").expect("fixture server: bind 127.0.0.1:0");
        Self::serve(listener, routes)
    }

    /// Binds `[::1]:0` only (SI-22); a bind failure panics.
    pub fn start_v6(routes: Vec<(String, Reply)>) -> FixtureServer {
        let listener = TcpListener::bind("[::1]:0").expect("fixture server: bind [::1]:0");
        Self::serve(listener, routes)
    }

    fn serve(listener: TcpListener, routes: Vec<(String, Reply)>) -> FixtureServer {
        let addr = listener.local_addr().expect("fixture server: local_addr");
        let shared = Arc::new(Shared {
            routes: Mutex::new(routes),
            requests: Mutex::new(Vec::new()),
            hits: Mutex::new(BTreeMap::new()),
            stop: Mutex::new(false),
            wake: Condvar::new(),
            workers: Mutex::new(Vec::new()),
        });
        let for_accept = Arc::clone(&shared);
        let accept = thread::Builder::new()
            .name("fixture-accept".to_owned())
            .spawn(move || accept_loop(&listener, &for_accept))
            .expect("fixture server: spawn the accept thread");
        FixtureServer {
            addr,
            shared,
            accept: Some(accept),
        }
    }

    /// `http://127.0.0.1:<port><path>` or `http://[::1]:<port><path>`.
    pub fn url(&self, path: &str) -> String {
        format!("http://{}{path}", self.addr)
    }

    pub fn port(&self) -> u16 {
        self.addr.port()
    }

    /// PD-7: replaces the reply for `path` (or adds it), at the same port.
    pub fn set_route(&self, path: &str, reply: Reply) {
        let mut routes = lock(&self.shared.routes);
        match routes.iter_mut().find(|(p, _)| p == path) {
            Some(entry) => entry.1 = reply,
            None => routes.push((path.to_owned(), reply)),
        }
    }

    pub fn requests(&self) -> Vec<RecordedRequest> {
        lock(&self.shared.requests).clone()
    }
}

impl Drop for FixtureServer {
    fn drop(&mut self) {
        *lock(&self.shared.stop) = true;
        self.shared.wake.notify_all();
        if let Some(accept) = self.accept.take() {
            // Wake the blocked accept with one connection to our own listener; if even that fails the accept thread is
            // left detached rather than joined forever.
            if TcpStream::connect(self.addr).is_ok() {
                // A panicked worker already failed its own test through the client's view.
                let _ = accept.join();
            }
        }
        let workers = std::mem::take(&mut *lock(&self.shared.workers));
        for worker in workers {
            let _ = worker.join();
        }
    }
}

fn accept_loop(listener: &TcpListener, shared: &Arc<Shared>) {
    for incoming in listener.incoming() {
        if shared.stopping() {
            break;
        }
        let Ok(stream) = incoming else { continue };
        let for_worker = Arc::clone(shared);
        let spawned = thread::Builder::new()
            .name("fixture-conn".to_owned())
            .spawn(move || serve_connection(stream, &for_worker));
        if let Ok(handle) = spawned {
            lock(&shared.workers).push(handle);
        }
    }
}

/// One accepted connection: its stream and the bytes read but not yet consumed.
struct Conn<'a> {
    stream: TcpStream,
    buf: Vec<u8>,
    shared: &'a Shared,
}

impl Conn<'_> {
    /// Reads more bytes into `buf`; `false` on EOF, error, shutdown or a passed `deadline`.
    fn fill(&mut self, deadline: Option<Instant>) -> bool {
        let mut chunk = [0u8; 4096];
        loop {
            if self.shared.stopping() || deadline.is_some_and(|d| Instant::now() >= d) {
                return false;
            }
            match self.stream.read(&mut chunk) {
                Ok(0) => return false,
                Ok(n) => {
                    self.buf.extend_from_slice(&chunk[..n]);
                    return true;
                }
                Err(e)
                    if matches!(
                        e.kind(),
                        ErrorKind::WouldBlock | ErrorKind::TimedOut | ErrorKind::Interrupted
                    ) => {}
                Err(_) => return false,
            }
        }
    }

    /// Fills until `buf` holds at least `n` bytes.
    fn ensure(&mut self, n: usize) -> bool {
        while self.buf.len() < n {
            if !self.fill(None) {
                return false;
            }
        }
        true
    }

    fn send(&mut self, bytes: &[u8]) -> bool {
        self.stream.write_all(bytes).is_ok() && self.stream.flush().is_ok()
    }

    /// `0x16` first: read the one record (bounded), answer the fixed 400, half-close, read to EOF or 2 s.
    fn answer_tls(&mut self) {
        if !self.ensure(5) {
            return;
        }
        let declared = usize::from(u16::from_be_bytes([self.buf[3], self.buf[4]]));
        if !self.ensure(5 + declared.min(MAX_TLS_RECORD)) {
            return;
        }
        if !self.send(TLS_ANSWER) {
            return;
        }
        let _ = self.stream.shutdown(Shutdown::Write);
        let deadline = Instant::now() + TLS_DRAIN;
        self.buf.clear();
        while self.fill(Some(deadline)) {
            self.buf.clear();
        }
    }

    /// Reads up to and including CRLFCRLF and removes it from `buf`; `None` on EOF, shutdown or an oversized head.
    fn read_head(&mut self) -> Option<Vec<u8>> {
        loop {
            if let Some(end) = self.buf.windows(4).position(|w| w == b"\r\n\r\n") {
                let rest = self.buf.split_off(end + 4);
                return Some(std::mem::replace(&mut self.buf, rest));
            }
            if self.buf.len() > MAX_HEAD {
                self.send(&status_response(431, &[]));
                return None;
            }
            if !self.fill(None) {
                return None;
            }
        }
    }

    /// Answers `reply` to `request`; `true` when the connection is to be served again from the top (`ConnectOk`,
    /// PD-7).
    fn answer(&mut self, reply: &Reply, request: &RecordedRequest) -> bool {
        match reply {
            Reply::Ok { body, content_type } => {
                let head = format!(
                    "HTTP/1.1 200 OK\r\nContent-Type: {content_type}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                    body.len()
                );
                self.send(&[head.as_bytes(), body].concat());
            }
            Reply::Status(status) => {
                self.send(&status_response(*status, &[]));
            }
            Reply::Redirect { status, location } => {
                self.send(&status_response(*status, &[("Location", location)]));
            }
            Reply::Truncated { declared_len, sent } => {
                self.send(&[ok_head(*declared_len, &[]), sent.clone()].concat());
            }
            Reply::Stall(d) => {
                self.shared.wait(*d);
            }
            Reply::Close => {}
            Reply::Garbage(bytes) => {
                self.send(bytes);
            }
            Reply::Gzip { body } => {
                self.send(
                    &[
                        ok_head(body.len(), &[("Content-Encoding", "gzip")]),
                        body.clone(),
                    ]
                    .concat(),
                );
            }
            Reply::ConnectOk => {
                return self.send(b"HTTP/1.1 200 Connection established\r\n\r\n");
            }
            Reply::ConnectRefused(status) => {
                self.send(&status_response(*status, &[]));
            }
            Reply::HeadersThenStall {
                declared_len,
                sent,
                stall,
            } => {
                if self.send(&[ok_head(*declared_len, &[]), sent.clone()].concat()) {
                    self.shared.wait(*stall);
                }
            }
            Reply::Delayed { after, reply } => {
                if !self.shared.wait(*after) {
                    return self.answer(reply, request);
                }
            }
            Reply::Ranged { body } => {
                let len = body.len();
                match range_start(request) {
                    Some(n) if n < len => {
                        let head = format!(
                            "HTTP/1.1 206 {}\r\nContent-Range: bytes {n}-{}/{len}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                            reason(206),
                            len - 1,
                            len - n
                        );
                        self.send(&[head.as_bytes(), &body[n..]].concat());
                    }
                    Some(_) => {
                        let range = format!("bytes */{len}");
                        self.send(&status_response(416, &[("Content-Range", &range)]));
                    }
                    None => {
                        self.send(&[ok_head(len, &[]), body.clone()].concat());
                    }
                }
            }
            Reply::Script(_) => {
                unreachable!(
                    "reply_for resolves a top-level Script; a nested Script is not supported"
                )
            }
            Reply::Paced {
                body,
                chunk,
                interval,
            } => {
                // Every request, ranged or not, gets the whole 200 body (SC-037).
                if self.send(&ok_head(body.len(), &[])) {
                    for (i, part) in body.chunks((*chunk).max(1)).enumerate() {
                        if (i > 0 && self.shared.wait(*interval)) || !self.send(part) {
                            break;
                        }
                    }
                }
            }
            Reply::Held { gate, reply } => {
                let ticket = gate.enter(&request.path);
                if gate.wait_release(ticket, self.shared) {
                    return self.answer(reply, request);
                }
            }
        }
        false
    }
}

fn serve_connection(stream: TcpStream, shared: &Shared) {
    if stream.set_read_timeout(Some(POLL)).is_err()
        || stream.set_write_timeout(Some(WRITE_TIMEOUT)).is_err()
    {
        return;
    }
    let _ = stream.set_nodelay(true);
    let mut conn = Conn {
        stream,
        buf: Vec::new(),
        shared,
    };
    loop {
        if !conn.ensure(1) {
            return;
        }
        if conn.buf[0] == 0x16 {
            conn.answer_tls();
            return;
        }
        let Some(head) = conn.read_head() else { return };
        let Some(request) = parse_head(&head) else {
            conn.send(&status_response(400, &[]));
            break;
        };
        let reply = if request.method == "CONNECT" && !is_loopback_authority(&request.path) {
            // Never tunnelled, whatever the route table holds: nothing is dialled (SI-22).
            Reply::Status(403)
        } else {
            shared.reply_for(&request.path)
        };
        lock(&shared.requests).push(request.clone());
        if !conn.answer(&reply, &request) {
            break;
        }
    }
    let _ = conn.stream.shutdown(Shutdown::Write);
}

/// `127.0.0.1:<port>` or `[::1]:<port>` exactly (`localhost` and every DNS name excluded).
fn is_loopback_authority(authority: &str) -> bool {
    authority
        .strip_prefix("127.0.0.1:")
        .or_else(|| authority.strip_prefix("[::1]:"))
        .is_some_and(|port| {
            !port.is_empty()
                && port.bytes().all(|b| b.is_ascii_digit())
                && port.parse::<u16>().is_ok()
        })
}

/// `n` of the first `Range` header (name in any ASCII case) when it is exactly `bytes=<digits>-`; digits too large
/// for `usize` read as `usize::MAX` (past any body). `None` for no `Range` or another form.
fn range_start(request: &RecordedRequest) -> Option<usize> {
    let (_, value) = request
        .headers
        .iter()
        .find(|(name, _)| name.eq_ignore_ascii_case("Range"))?;
    let digits = value.strip_prefix("bytes=")?.strip_suffix('-')?;
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    Some(digits.parse().unwrap_or(usize::MAX))
}

/// `METHOD SP target SP version`, then `name: value` lines; `None` when the head is not that shape.
fn parse_head(head: &[u8]) -> Option<RecordedRequest> {
    let text = String::from_utf8_lossy(head);
    let mut lines = text.split("\r\n").filter(|l| !l.is_empty());
    let mut parts = lines.next()?.split(' ');
    let (method, path, version) = (parts.next()?, parts.next()?, parts.next()?);
    if parts.next().is_some()
        || method.is_empty()
        || path.is_empty()
        || !version.starts_with("HTTP/")
    {
        return None;
    }
    let headers = lines
        .map(|line| {
            line.split_once(':')
                .map(|(name, value)| (name.to_owned(), value.trim().to_owned()))
        })
        .collect::<Option<Vec<_>>>()?;
    Some(RecordedRequest {
        method: method.to_owned(),
        path: path.to_owned(),
        headers,
    })
}

fn reason(status: u16) -> &'static str {
    match status {
        200 => "OK",
        204 => "No Content",
        206 => "Partial Content",
        301 => "Moved Permanently",
        302 => "Found",
        303 => "See Other",
        307 => "Temporary Redirect",
        308 => "Permanent Redirect",
        400 => "Bad Request",
        403 => "Forbidden",
        404 => "Not Found",
        407 => "Proxy Authentication Required",
        408 => "Request Timeout",
        416 => "Range Not Satisfiable",
        429 => "Too Many Requests",
        431 => "Request Header Fields Too Large",
        500 => "Internal Server Error",
        502 => "Bad Gateway",
        503 => "Service Unavailable",
        504 => "Gateway Timeout",
        _ => "Status",
    }
}

/// A body-less response: `Content-Length: 0`, `Connection: close`.
fn status_response(status: u16, extra: &[(&str, &str)]) -> Vec<u8> {
    let mut head = format!("HTTP/1.1 {status} {}\r\n", reason(status));
    for (name, value) in extra {
        head.push_str(&format!("{name}: {value}\r\n"));
    }
    head.push_str("Content-Length: 0\r\nConnection: close\r\n\r\n");
    head.into_bytes()
}

/// A 200 head declaring `len` body bytes.
fn ok_head(len: usize, extra: &[(&str, &str)]) -> Vec<u8> {
    let mut head = String::from("HTTP/1.1 200 OK\r\n");
    for (name, value) in extra {
        head.push_str(&format!("{name}: {value}\r\n"));
    }
    head.push_str(&format!(
        "Content-Length: {len}\r\nConnection: close\r\n\r\n"
    ));
    head.into_bytes()
}

#[cfg(test)]
#[path = "fixture_server_tests.rs"]
mod tests;
