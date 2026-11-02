//! The SOCKS4 / SOCKS5 relay of `socks-proxy` (PD-30, SI-30): std only, no-auth CONNECT to IPv4 `127.0.0.1` only.
//! Any other target gets the refusal reply and is recorded as refused; a first byte that is not a SOCKS version is
//! recorded as `not socks`. An accepted target is dialled (the harness's only `TcpStream::connect`) and bytes are
//! copied both ways until either side closes. `Drop` stops the accept loop and closes every stream.

use std::io::{self, Read, Write};
use std::net::{Ipv4Addr, Shutdown, SocketAddr, SocketAddrV4, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};
use std::thread::{self, JoinHandle};
use std::time::Duration;

/// How often the accept loop re-checks the stop flag.
const POLL: Duration = Duration::from_millis(5);
/// The bound of every handshake read.
const HANDSHAKE_TIMEOUT: Duration = Duration::from_secs(5);

/// One accepted CONNECT.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SocksConnect {
    pub version: u8,
    pub target: SocketAddr,
}

#[derive(Default)]
struct Shared {
    connects: Mutex<Vec<SocksConnect>>,
    refused: Mutex<Vec<String>>,
    not_socks: Mutex<Vec<String>>,
    streams: Mutex<Vec<TcpStream>>,
    workers: Mutex<Vec<JoinHandle<()>>>,
    stop: AtomicBool,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

pub struct SocksRelay {
    addr: SocketAddr,
    shared: Arc<Shared>,
    accept: Option<JoinHandle<()>>,
}

impl SocksRelay {
    /// Serves `listener` (from `bind_loopback_listener`), one thread per connection.
    pub fn start(listener: TcpListener) -> SocksRelay {
        let addr = listener.local_addr().expect("relay: local_addr");
        listener
            .set_nonblocking(true)
            .expect("relay: nonblocking listener");
        let shared = Arc::new(Shared::default());
        let for_accept = Arc::clone(&shared);
        let accept = thread::Builder::new()
            .name("socks-accept".to_owned())
            .spawn(move || accept_loop(&listener, &for_accept))
            .expect("relay: spawn the accept thread");
        SocksRelay {
            addr,
            shared,
            accept: Some(accept),
        }
    }

    /// `<scheme>://127.0.0.1:<port>`.
    pub fn url(&self, scheme: &str) -> String {
        format!("{scheme}://{}", self.addr)
    }

    pub fn connects(&self) -> Vec<SocksConnect> {
        lock(&self.shared.connects).clone()
    }

    pub fn refused(&self) -> Vec<String> {
        lock(&self.shared.refused).clone()
    }

    pub fn not_socks(&self) -> Vec<String> {
        lock(&self.shared.not_socks).clone()
    }
}

impl Drop for SocksRelay {
    fn drop(&mut self) {
        self.shared.stop.store(true, Ordering::SeqCst);
        if let Some(h) = self.accept.take() {
            let _ = h.join();
        }
        for s in lock(&self.shared.streams).drain(..) {
            let _ = s.shutdown(Shutdown::Both);
        }
        let workers: Vec<JoinHandle<()>> = lock(&self.shared.workers).drain(..).collect();
        for w in workers {
            let _ = w.join();
        }
    }
}

fn accept_loop(listener: &TcpListener, shared: &Arc<Shared>) {
    while !shared.stop.load(Ordering::SeqCst) {
        match listener.accept() {
            Ok((stream, _)) => {
                let for_conn = Arc::clone(shared);
                let worker = thread::Builder::new()
                    .name("socks-conn".to_owned())
                    .spawn(move || serve(stream, &for_conn));
                if let Ok(w) = worker {
                    lock(&shared.workers).push(w);
                }
            }
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => thread::sleep(POLL),
            Err(_) => thread::sleep(POLL),
        }
    }
}

fn read_exact<const N: usize>(s: &mut TcpStream) -> io::Result<[u8; N]> {
    let mut buf = [0u8; N];
    s.read_exact(&mut buf)?;
    Ok(buf)
}

/// Reads a SOCKS4 user id up to and including its NUL.
fn read_userid(s: &mut TcpStream) -> io::Result<()> {
    loop {
        let [b] = read_exact::<1>(s)?;
        if b == 0 {
            return Ok(());
        }
    }
}

/// The handshake's CONNECT target, or the refusal already answered and recorded.
fn handshake(client: &mut TcpStream, shared: &Shared) -> io::Result<Option<(u8, SocketAddrV4)>> {
    let [version] = read_exact::<1>(client)?;
    match version {
        5 => {
            let [n] = read_exact::<1>(client)?;
            let mut methods = vec![0u8; usize::from(n)];
            client.read_exact(&mut methods)?;
            if !methods.contains(&0) {
                client.write_all(&[5, 0xFF])?;
                lock(&shared.refused).push(format!("socks5 methods {methods:?}"));
                return Ok(None);
            }
            client.write_all(&[5, 0])?;
            let [ver, cmd, _rsv, atyp] = read_exact::<4>(client)?;
            let target = if ver == 5 && cmd == 1 && atyp == 1 {
                let [a, b, c, d, p0, p1] = read_exact::<6>(client)?;
                Some(SocketAddrV4::new(
                    Ipv4Addr::new(a, b, c, d),
                    u16::from_be_bytes([p0, p1]),
                ))
            } else {
                None
            };
            match target {
                Some(t) if *t.ip() == Ipv4Addr::LOCALHOST => Ok(Some((5, t))),
                other => {
                    client.write_all(&[5, 2, 0, 1, 0, 0, 0, 0, 0, 0])?;
                    lock(&shared.refused)
                        .push(format!("socks5 cmd {cmd} atyp {atyp} target {other:?}"));
                    Ok(None)
                }
            }
        }
        4 => {
            let [cmd, p0, p1, a, b, c, d] = read_exact::<7>(client)?;
            read_userid(client)?;
            let t = SocketAddrV4::new(Ipv4Addr::new(a, b, c, d), u16::from_be_bytes([p0, p1]));
            if cmd == 1 && *t.ip() == Ipv4Addr::LOCALHOST {
                Ok(Some((4, t)))
            } else {
                client.write_all(&[0, 0x5B, 0, 0, 0, 0, 0, 0])?;
                lock(&shared.refused).push(format!("socks4 cmd {cmd} target {t}"));
                Ok(None)
            }
        }
        other => {
            lock(&shared.not_socks).push(format!("first byte 0x{other:02x}"));
            Ok(None)
        }
    }
}

fn success_reply(version: u8, t: SocketAddrV4) -> Vec<u8> {
    let [p0, p1] = t.port().to_be_bytes();
    let [a, b, c, d] = t.ip().octets();
    if version == 5 {
        vec![5, 0, 0, 1, a, b, c, d, p0, p1]
    } else {
        vec![0, 0x5A, p0, p1, a, b, c, d]
    }
}

fn copy_one_way(mut from: TcpStream, mut to: TcpStream) {
    let _ = io::copy(&mut from, &mut to);
    let _ = to.shutdown(Shutdown::Write);
}

fn serve(mut client: TcpStream, shared: &Shared) {
    let _ = client.set_nonblocking(false);
    let _ = client.set_read_timeout(Some(HANDSHAKE_TIMEOUT));
    if let Ok(c) = client.try_clone() {
        lock(&shared.streams).push(c);
    }
    let Ok(Some((version, target))) = handshake(&mut client, shared) else {
        return;
    };
    let upstream = match TcpStream::connect(SocketAddr::V4(target)) {
        Ok(s) => s,
        Err(e) => {
            let refusal: &[u8] = if version == 5 {
                &[5, 5, 0, 1, 0, 0, 0, 0, 0, 0]
            } else {
                &[0, 0x5B, 0, 0, 0, 0, 0, 0]
            };
            let _ = client.write_all(refusal);
            lock(&shared.refused).push(format!("dial {target}: {e}"));
            return;
        }
    };
    if let Ok(u) = upstream.try_clone() {
        lock(&shared.streams).push(u);
    }
    if client.write_all(&success_reply(version, target)).is_err() {
        return;
    }
    lock(&shared.connects).push(SocksConnect {
        version,
        target: SocketAddr::V4(target),
    });
    let _ = client.set_read_timeout(None);
    let (Ok(client_r), Ok(upstream_w)) = (client.try_clone(), upstream.try_clone()) else {
        return;
    };
    let up = thread::spawn(move || copy_one_way(client_r, upstream_w));
    copy_one_way(upstream, client);
    let _ = up.join();
}
