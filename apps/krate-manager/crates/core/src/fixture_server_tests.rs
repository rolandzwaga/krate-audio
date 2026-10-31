//! fixture_server tests (plan §4.7, TA-8): raw `std::net::TcpStream` clients, each test its own servers.

use super::*;
use flate2::Compression;
use flate2::write::GzEncoder;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::thread;
use std::time::Instant;

/// Longer than any test may take: a stall that is not ended by `Drop` fails the 1 s bound.
const LONG: Duration = Duration::from_secs(30);
/// A client read that waits this long has hung: the test fails instead of blocking.
const CLIENT_TIMEOUT: Duration = Duration::from_secs(5);
const DROP_BOUND: Duration = Duration::from_secs(1);

fn open(server: &FixtureServer) -> TcpStream {
    let stream = TcpStream::connect(server.addr).expect("connect to the fixture server");
    stream
        .set_read_timeout(Some(CLIENT_TIMEOUT))
        .expect("client read timeout");
    stream
}

fn send(server: &FixtureServer, raw: &[u8]) -> TcpStream {
    let mut stream = open(server);
    stream.write_all(raw).expect("client write");
    stream
}

fn read_all(mut stream: TcpStream) -> Vec<u8> {
    let mut out = Vec::new();
    stream.read_to_end(&mut out).expect("client read to EOF");
    out
}

fn get_request(path: &str) -> Vec<u8> {
    format!("GET {path} HTTP/1.1\r\nHost: fixture\r\n\r\n").into_bytes()
}

fn get(server: &FixtureServer, path: &str) -> Vec<u8> {
    read_all(send(server, &get_request(path)))
}

/// Reads one byte at a time up to and including the first CRLFCRLF.
fn read_head(stream: &mut TcpStream) -> Vec<u8> {
    let mut head = Vec::new();
    let mut byte = [0u8; 1];
    while !head.ends_with(b"\r\n\r\n") {
        stream.read_exact(&mut byte).expect("client read of a head");
        head.push(byte[0]);
    }
    head
}

fn read_n(stream: &mut TcpStream, n: usize) -> Vec<u8> {
    let mut out = vec![0u8; n];
    stream.read_exact(&mut out).expect("client read_exact");
    out
}

struct Response {
    status_line: String,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}

impl Response {
    fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .iter()
            .find(|(n, _)| n.eq_ignore_ascii_case(name))
            .map(|(_, v)| v.as_str())
    }
}

fn parse(raw: &[u8]) -> Response {
    let end = raw
        .windows(4)
        .position(|w| w == b"\r\n\r\n")
        .unwrap_or_else(|| panic!("no CRLFCRLF in {:?}", String::from_utf8_lossy(raw)));
    let head = String::from_utf8(raw[..end].to_vec()).expect("an ASCII head");
    let mut lines = head.split("\r\n");
    let status_line = lines.next().expect("a status line").to_owned();
    let headers = lines
        .map(|line| {
            let (name, value) = line.split_once(':').expect("a header line");
            (name.to_owned(), value.trim().to_owned())
        })
        .collect();
    Response {
        status_line,
        headers,
        body: raw[end + 4..].to_vec(),
    }
}

/// A final response: parsed, and carrying `Connection: close` (plan §4.7, no keep-alive).
fn response(raw: &[u8]) -> Response {
    let r = parse(raw);
    assert_eq!(
        r.header("Connection"),
        Some("close"),
        "{} lacks Connection: close",
        r.status_line
    );
    r
}

fn wait_for_requests(server: &FixtureServer, n: usize) {
    let deadline = Instant::now() + CLIENT_TIMEOUT;
    while server.requests().len() < n {
        assert!(
            Instant::now() < deadline,
            "the server recorded {:?}, waiting for {n}",
            server.requests()
        );
        thread::sleep(Duration::from_millis(5));
    }
}

/// Drops the server, asserts the drop returned within `DROP_BOUND`, and returns what the client then reads to EOF.
fn drop_within_bound(server: FixtureServer, stream: TcpStream, what: &str) -> Vec<u8> {
    let started = Instant::now();
    drop(server);
    let elapsed = started.elapsed();
    assert!(elapsed < DROP_BOUND, "{what}: drop took {elapsed:?}");
    read_all(stream)
}

fn route(path: &str, reply: Reply) -> (String, Reply) {
    (path.to_owned(), reply)
}

fn connect_request(authority: &str) -> Vec<u8> {
    format!("CONNECT {authority} HTTP/1.1\r\nHost: {authority}\r\n\r\n").into_bytes()
}

#[test]
fn binds_loopback_port_zero() {
    let a = FixtureServer::start(vec![]);
    let b = FixtureServer::start(vec![]);
    assert!(
        a.url("/x").starts_with("http://127.0.0.1:"),
        "{}",
        a.url("/x")
    );
    assert_eq!(a.url("/x"), format!("http://127.0.0.1:{}/x", a.port()));
    assert_ne!(a.port(), 0);
    assert_ne!(b.port(), 0);
    assert_ne!(
        a.port(),
        b.port(),
        "two servers started at once share a port"
    );
    assert!(a.addr.ip().is_loopback());

    // A bind failure panics inside start_v6: never skipped (plan §9 last row).
    let v6 = FixtureServer::start_v6(vec![]);
    assert!(
        v6.url("/x").starts_with("http://[::1]:"),
        "{}",
        v6.url("/x")
    );
    assert_eq!(v6.url("/x"), format!("http://[::1]:{}/x", v6.port()));
    assert_ne!(v6.port(), 0);
    assert!(v6.addr.is_ipv6() && v6.addr.ip().is_loopback());

    // Both are live: an empty route table answers 404.
    for server in [&a, &b, &v6] {
        let r = response(&get(server, "/x"));
        assert!(
            r.status_line.starts_with("HTTP/1.1 404"),
            "{}",
            r.status_line
        );
    }
}

#[test]
fn connect_to_remote_host_is_refused() {
    for authority in ["example.invalid:443", "localhost:443"] {
        let server = FixtureServer::start(vec![
            route("example.invalid:443", Reply::ConnectOk),
            route("localhost:443", Reply::ConnectOk),
        ]);
        // read_all returns only at EOF: the refusal closes the stream.
        let raw = read_all(send(&server, &connect_request(authority)));
        let r = response(&raw);
        assert!(
            r.status_line.starts_with("HTTP/1.1 403"),
            "{authority}: {}",
            r.status_line
        );
        assert!(
            r.body.is_empty(),
            "{authority}: tunnelled bytes {:?}",
            r.body
        );
        let recorded = server.requests();
        assert_eq!(recorded.len(), 1, "{authority}: {recorded:?}");
        assert_eq!(recorded[0].method, "CONNECT");
        assert_eq!(recorded[0].path, authority);
        assert_eq!(
            recorded[0].headers,
            vec![("Host".to_owned(), authority.to_owned())]
        );
    }
}

#[test]
fn replies_behave_as_scripted() {
    let mut gz = GzEncoder::new(Vec::new(), Compression::default());
    gz.write_all(b"{\"schema_version\":2}").expect("gzip");
    let gzipped = gz.finish().expect("gzip finish");

    let server = FixtureServer::start(vec![
        route(
            "/ok",
            Reply::Ok {
                body: b"hello".to_vec(),
                content_type: "application/json",
            },
        ),
        route("/503", Reply::Status(503)),
        route(
            "/a",
            Reply::Redirect {
                status: 302,
                location: "/b".to_owned(),
            },
        ),
        route(
            "/trunc",
            Reply::Truncated {
                declared_len: 100,
                sent: b"0123456789".to_vec(),
            },
        ),
        route("/close", Reply::Close),
        route("/garbage", Reply::Garbage(b"xx".to_vec())),
        route(
            "/gz",
            Reply::Gzip {
                body: gzipped.clone(),
            },
        ),
        route(
            "/v",
            Reply::Ok {
                body: b"tunnelled".to_vec(),
                content_type: "text/plain",
            },
        ),
        route("127.0.0.1:9", Reply::ConnectRefused(407)),
    ]);

    // Ok
    let r = response(&get(&server, "/ok"));
    assert_eq!(r.status_line, "HTTP/1.1 200 OK");
    assert_eq!(r.header("Content-Length"), Some("5"));
    assert_eq!(r.header("Content-Type"), Some("application/json"));
    assert_eq!(r.body, b"hello");

    // Status(503): empty body
    let r = response(&get(&server, "/503"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 503"),
        "{}",
        r.status_line
    );
    assert_eq!(r.header("Content-Length"), Some("0"));
    assert!(r.body.is_empty());

    // Redirect
    let r = response(&get(&server, "/a"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 302"),
        "{}",
        r.status_line
    );
    assert_eq!(r.header("Location"), Some("/b"));
    assert!(r.body.is_empty());

    // Truncated: header 100, EOF after 10
    let r = response(&get(&server, "/trunc"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 200"),
        "{}",
        r.status_line
    );
    assert_eq!(r.header("Content-Length"), Some("100"));
    assert_eq!(r.body, b"0123456789");

    // Close: EOF, no bytes
    assert!(get(&server, "/close").is_empty());

    // Garbage: exactly those bytes
    assert_eq!(get(&server, "/garbage"), b"xx");

    // Gzip: the bytes verbatim
    let r = response(&get(&server, "/gz"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 200"),
        "{}",
        r.status_line
    );
    assert_eq!(r.header("Content-Encoding"), Some("gzip"));
    assert_eq!(
        r.header("Content-Length"),
        Some(gzipped.len().to_string().as_str())
    );
    assert_eq!(r.body, gzipped);

    // Unknown path
    let r = response(&get(&server, "/nowhere"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 404"),
        "{}",
        r.status_line
    );
    assert!(r.body.is_empty());

    // A TLS record (first byte 0x16, a 5-byte header declaring 3 bytes, those bytes): the fixed 400, then EOF.
    let raw = read_all(send(
        &server,
        &[0x16, 0x03, 0x01, 0x00, 0x03, 0xaa, 0xbb, 0xcc],
    ));
    assert_eq!(
        raw,
        b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );

    // CONNECT 127.0.0.1:<port> → ConnectOk, then the same stream is served again from the route table (PD-7).
    let authority = format!("127.0.0.1:{}", server.port());
    server.set_route(&authority, Reply::ConnectOk);
    let mut tunnel = send(&server, &connect_request(&authority));
    let head = parse(&read_head(&mut tunnel));
    assert_eq!(head.status_line, "HTTP/1.1 200 Connection established");
    tunnel
        .write_all(&get_request("/v"))
        .expect("tunnelled write");
    let r = response(&read_all(tunnel));
    assert_eq!(r.status_line, "HTTP/1.1 200 OK");
    assert_eq!(r.body, b"tunnelled");

    // ConnectRefused(407)
    let r = response(&read_all(send(&server, &connect_request("127.0.0.1:9"))));
    assert!(
        r.status_line.starts_with("HTTP/1.1 407"),
        "{}",
        r.status_line
    );
    assert!(r.body.is_empty());

    // set_route replaces a reply at the same port, and adds a new path.
    let port = server.port();
    server.set_route(
        "/503",
        Reply::Ok {
            body: b"now ok".to_vec(),
            content_type: "text/plain",
        },
    );
    server.set_route("/new", Reply::Status(204));
    assert_eq!(server.port(), port);
    let r = response(&get(&server, "/503"));
    assert_eq!(r.status_line, "HTTP/1.1 200 OK");
    assert_eq!(r.body, b"now ok");
    let r = response(&get(&server, "/new"));
    assert!(
        r.status_line.starts_with("HTTP/1.1 204"),
        "{}",
        r.status_line
    );

    // requests(): method, path and headers, in order (the TLS record is not a request).
    let recorded = server.requests();
    let paths: Vec<(&str, &str)> = recorded
        .iter()
        .map(|q| (q.method.as_str(), q.path.as_str()))
        .collect();
    assert_eq!(
        paths,
        vec![
            ("GET", "/ok"),
            ("GET", "/503"),
            ("GET", "/a"),
            ("GET", "/trunc"),
            ("GET", "/close"),
            ("GET", "/garbage"),
            ("GET", "/gz"),
            ("GET", "/nowhere"),
            ("CONNECT", authority.as_str()),
            ("GET", "/v"),
            ("CONNECT", "127.0.0.1:9"),
            ("GET", "/503"),
            ("GET", "/new"),
        ]
    );
    let host = vec![("Host".to_owned(), "fixture".to_owned())];
    assert_eq!(
        recorded[0],
        RecordedRequest {
            method: "GET".to_owned(),
            path: "/ok".to_owned(),
            headers: host.clone(),
        }
    );
    let raw = read_all(send(
        &server,
        b"HEAD /ok HTTP/1.1\r\nHost: h\r\nX-One:  1 \r\nX-Two: two\r\n\r\n",
    ));
    assert!(response(&raw).status_line.starts_with("HTTP/1.1 200"));
    assert_eq!(
        server.requests().last(),
        Some(&RecordedRequest {
            method: "HEAD".to_owned(),
            path: "/ok".to_owned(),
            headers: vec![
                ("Host".to_owned(), "h".to_owned()),
                ("X-One".to_owned(), "1".to_owned()),
                ("X-Two".to_owned(), "two".to_owned()),
            ],
        })
    );
    drop(server);

    // Stall(30 s): dropping the server ends it at once; the client sees EOF and no bytes.
    let server = FixtureServer::start(vec![route("/s", Reply::Stall(LONG))]);
    let stream = send(&server, &get_request("/s"));
    wait_for_requests(&server, 1);
    assert!(drop_within_bound(server, stream, "Stall").is_empty());

    // Delayed { after: 30 s, … }: dropping ends the wait, nothing is sent.
    let server = FixtureServer::start(vec![route(
        "/d",
        Reply::Delayed {
            after: LONG,
            reply: Box::new(Reply::Redirect {
                status: 302,
                location: "/b".to_owned(),
            }),
        },
    )]);
    let stream = send(&server, &get_request("/d"));
    wait_for_requests(&server, 1);
    assert!(drop_within_bound(server, stream, "Delayed").is_empty());

    // Delayed with a short wait answers with the inner reply.
    let server = FixtureServer::start(vec![route(
        "/d",
        Reply::Delayed {
            after: Duration::from_millis(50),
            reply: Box::new(Reply::Redirect {
                status: 302,
                location: "/b".to_owned(),
            }),
        },
    )]);
    let started = Instant::now();
    let r = response(&get(&server, "/d"));
    assert!(started.elapsed() >= Duration::from_millis(50));
    assert!(
        r.status_line.starts_with("HTTP/1.1 302"),
        "{}",
        r.status_line
    );
    assert_eq!(r.header("Location"), Some("/b"));
    drop(server);

    // HeadersThenStall { 100, 10 bytes, 30 s }: the head and the 10 bytes arrive, then dropping ends the stall.
    let server = FixtureServer::start(vec![route(
        "/hs",
        Reply::HeadersThenStall {
            declared_len: 100,
            sent: b"abcdefghij".to_vec(),
            stall: LONG,
        },
    )]);
    let mut stream = send(&server, &get_request("/hs"));
    let head = response(&read_head(&mut stream));
    assert!(
        head.status_line.starts_with("HTTP/1.1 200"),
        "{}",
        head.status_line
    );
    assert_eq!(head.header("Content-Length"), Some("100"));
    assert_eq!(read_n(&mut stream, 10), b"abcdefghij");
    assert!(drop_within_bound(server, stream, "HeadersThenStall").is_empty());
}
