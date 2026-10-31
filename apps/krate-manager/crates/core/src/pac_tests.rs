//! PAC tests (plan §4.15, §7.1) and the shared `FakePacHost` (TA-12, TA-22), compiled on every OS. `http_tests.rs`
//! and `feed_tests.rs` build their PAC hosts through `crate::pac::tests::FakePacHost::handle`.

use super::*;
use crate::test_support::{install_capture, take_mine};
use helpers::{Arg, Civil};
use std::collections::{HashMap, HashSet, VecDeque};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc::{self, Receiver};
use std::sync::{Condvar, Mutex, MutexGuard, PoisonError};
use std::thread;
use std::time::Instant;

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

/// One scripted `os_find_proxy` answer of an `os_mode()` fake.
pub(crate) enum OsAnswer {
    Entries(Vec<OsProxyEntry>),
    Fail(PacError),
    /// Blocks until the test's `Sender` is dropped (or sends), then `Err(PacError { Fetch, "released" })`.
    Block(Receiver<()>),
}

/// The release gate the hanging names share: their workers wait on it (TA-22).
struct Gate {
    released: Mutex<bool>,
    wake: Condvar,
    finished: AtomicUsize,
}

/// A scripted `PacHost` (plan §4.15 last paragraph): a name table, hanging names behind one gate, a fixed local
/// address and clock, injected WPAD candidates and, in OS mode, a queue of `os_find_proxy` answers. Every call is
/// logged with the wait it received.
pub(crate) struct FakePacHost {
    names: HashMap<String, Option<IpAddr>>,
    hangs: HashSet<String>,
    gate: Arc<Gate>,
    my_ip: IpAddr,
    clock: PacClock,
    now_calls: AtomicUsize,
    candidates: Vec<String>,
    os_mode: bool,
    os_queue: Mutex<VecDeque<OsAnswer>>,
    resolve_log: Mutex<Vec<(String, Duration)>>,
    candidate_waits: Mutex<Vec<Duration>>,
    os_calls: Mutex<Vec<(String, Duration)>>,
    os_finished: AtomicUsize,
}

impl FakePacHost {
    /// No names, no candidates, not in OS mode; `my_ip` 192.0.2.7 and the clock 1 700 000 000 s at +01:00.
    pub(crate) fn new() -> Self {
        FakePacHost {
            names: HashMap::new(),
            hangs: HashSet::new(),
            gate: Arc::new(Gate {
                released: Mutex::new(false),
                wake: Condvar::new(),
                finished: AtomicUsize::new(0),
            }),
            my_ip: IpAddr::V4(Ipv4Addr::new(192, 0, 2, 7)),
            clock: PacClock {
                unix_secs: 1_700_000_000,
                local_offset_secs: Some(3600),
            },
            now_calls: AtomicUsize::new(0),
            candidates: Vec::new(),
            os_mode: false,
            os_queue: Mutex::new(VecDeque::new()),
            resolve_log: Mutex::new(Vec::new()),
            candidate_waits: Mutex::new(Vec::new()),
            os_calls: Mutex::new(Vec::new()),
            os_finished: AtomicUsize::new(0),
        }
    }

    /// `resolve(name, _)` answers `answer` at once.
    pub(crate) fn name(mut self, name: &str, answer: Option<IpAddr>) -> Self {
        self.names.insert(name.to_owned(), answer);
        self
    }

    /// `resolve(name, wait)` never answers: its worker waits on the release gate, the call returns `None` after
    /// `wait`.
    pub(crate) fn hang(mut self, name: &str) -> Self {
        self.hangs.insert(name.to_owned());
        self
    }

    pub(crate) fn my_ip(mut self, ip: IpAddr) -> Self {
        self.my_ip = ip;
        self
    }

    pub(crate) fn clock(mut self, clock: PacClock) -> Self {
        self.clock = clock;
        self
    }

    pub(crate) fn candidates(mut self, candidates: &[&str]) -> Self {
        self.candidates = candidates.iter().map(|c| (*c).to_owned()).collect();
        self
    }

    /// `os_evaluates()` is true and `os_find_proxy` answers from the queue.
    pub(crate) fn os_mode(mut self) -> Self {
        self.os_mode = true;
        self
    }

    pub(crate) fn os_answer(self, answer: OsAnswer) -> Self {
        lock(&self.os_queue).push_back(answer);
        self
    }

    /// The fake and the handle `HttpConfig` carries: the only `PacHostHandle::new(` in a test source (TA-12).
    pub(crate) fn handle(self) -> (Arc<FakePacHost>, PacHostHandle) {
        let host = Arc::new(self);
        let handle = PacHostHandle::new(host.clone());
        (host, handle)
    }

    /// `(name, wait)` per `resolve`, in call order.
    pub(crate) fn resolve_log(&self) -> Vec<(String, Duration)> {
        lock(&self.resolve_log).clone()
    }

    /// The `wait` of each `wpad_candidates` call.
    pub(crate) fn candidate_waits(&self) -> Vec<Duration> {
        lock(&self.candidate_waits).clone()
    }

    pub(crate) fn now_calls(&self) -> usize {
        self.now_calls.load(Ordering::SeqCst)
    }

    /// `(url, wait)` per `os_find_proxy`, in call order.
    pub(crate) fn os_calls(&self) -> Vec<(String, Duration)> {
        lock(&self.os_calls).clone()
    }

    /// Lets every hanging worker finish.
    pub(crate) fn release_hangs(&self) {
        *lock(&self.gate.released) = true;
        self.gate.wake.notify_all();
    }

    /// Hanging workers that have finished (after `release_hangs`).
    pub(crate) fn hangs_finished(&self) -> usize {
        self.gate.finished.load(Ordering::SeqCst)
    }

    /// OS-mode `os_find_proxy` calls that have returned.
    pub(crate) fn os_finished(&self) -> usize {
        self.os_finished.load(Ordering::SeqCst)
    }
}

impl PacHost for FakePacHost {
    fn resolve(&self, name: &str, wait: Duration) -> Option<IpAddr> {
        lock(&self.resolve_log).push((name.to_owned(), wait));
        if !self.hangs.contains(name) {
            return self.names.get(name).copied().flatten();
        }
        let gate = Arc::clone(&self.gate);
        let (tx, rx) = mpsc::channel::<Option<IpAddr>>();
        thread::spawn(move || {
            let released = lock(&gate.released);
            let released = gate
                .wake
                .wait_while(released, |released| !*released)
                .unwrap_or_else(PoisonError::into_inner);
            drop(released);
            gate.finished.fetch_add(1, Ordering::SeqCst);
            // The caller gave up long ago: nobody receives this.
            tx.send(None).ok();
        });
        rx.recv_timeout(wait).ok().flatten()
    }

    fn my_ip(&self) -> IpAddr {
        self.my_ip
    }

    fn now(&self) -> PacClock {
        self.now_calls.fetch_add(1, Ordering::SeqCst);
        self.clock
    }

    fn wpad_candidates(&self, wait: Duration) -> Vec<String> {
        lock(&self.candidate_waits).push(wait);
        self.candidates.clone()
    }

    fn os_evaluates(&self) -> bool {
        self.os_mode
    }

    fn os_find_proxy(
        &self,
        _source: &PacSource,
        url: &str,
        wait: Duration,
    ) -> Result<Vec<OsProxyEntry>, PacError> {
        lock(&self.os_calls).push((url.to_owned(), wait));
        if !self.os_mode {
            return Err(PacError {
                code: PacCode::Fetch,
                detail: "no OS PAC evaluator".to_owned(),
            });
        }
        let next = lock(&self.os_queue).pop_front();
        let answer = match next {
            None => Err(PacError {
                code: PacCode::Fetch,
                detail: "the fake's OS answer queue is empty".to_owned(),
            }),
            Some(OsAnswer::Entries(entries)) => Ok(entries),
            Some(OsAnswer::Fail(error)) => Err(error),
            Some(OsAnswer::Block(rx)) => {
                rx.recv().ok();
                Err(PacError {
                    code: PacCode::Fetch,
                    detail: "released".to_owned(),
                })
            }
        };
        self.os_finished.fetch_add(1, Ordering::SeqCst);
        answer
    }
}

/// Polls `done` every 5 ms until it holds or `within` has passed; true when it held.
fn eventually(within: Duration, done: impl Fn() -> bool) -> bool {
    let start = Instant::now();
    while start.elapsed() < within {
        if done() {
            return true;
        }
        thread::sleep(Duration::from_millis(5));
    }
    done()
}

#[test]
fn fake_pac_host_answers_from_its_table() {
    let a_ip = IpAddr::V4(Ipv4Addr::new(10, 0, 0, 1));
    let my_ip = IpAddr::V4(Ipv4Addr::new(192, 0, 2, 7));
    let clock = PacClock {
        unix_secs: 1_700_000_000,
        local_offset_secs: Some(3600),
    };
    let (fake, handle) = FakePacHost::new()
        .name("a.test", Some(a_ip))
        .hang("slow.test")
        .my_ip(my_ip)
        .clock(clock)
        .candidates(&["http://127.0.0.1:1/w.dat"])
        .handle();
    let host = handle.get();

    // The name table, and the log of (name, wait) in order.
    assert_eq!(host.resolve("a.test", Duration::from_secs(1)), Some(a_ip));
    assert_eq!(host.resolve("b.test", Duration::from_millis(200)), None);
    assert_eq!(
        fake.resolve_log(),
        [
            ("a.test".to_owned(), Duration::from_secs(1)),
            ("b.test".to_owned(), Duration::from_millis(200)),
        ]
    );

    // A hanging name: the call gives up after its wait, its worker ends only once released.
    let start = Instant::now();
    assert_eq!(host.resolve("slow.test", Duration::from_millis(200)), None);
    let took = start.elapsed();
    assert!(
        took >= Duration::from_millis(200),
        "returned early: {took:?}"
    );
    assert!(
        took < Duration::from_secs(1),
        "not bounded by its wait: {took:?}"
    );
    assert_eq!(fake.hangs_finished(), 0);
    fake.release_hangs();
    assert!(
        eventually(Duration::from_secs(2), || fake.hangs_finished() == 1),
        "the hanging worker did not finish after release: {}",
        fake.hangs_finished()
    );
    assert_eq!(fake.resolve_log().len(), 3);

    // The fixed address and clock; now() is counted.
    assert_eq!(host.my_ip(), my_ip);
    assert_eq!(fake.now_calls(), 0);
    assert_eq!(host.now(), clock);
    assert_eq!(fake.now_calls(), 1);

    // The injected WPAD candidates, and the wait logged.
    assert_eq!(
        host.wpad_candidates(Duration::from_secs(5)),
        ["http://127.0.0.1:1/w.dat"]
    );
    assert_eq!(fake.candidate_waits(), [Duration::from_secs(5)]);

    // Not in OS mode: the OS path is refused as the trait default refuses it.
    let source = PacSource {
        auto_detect: true,
        url: None,
    };
    assert!(!host.os_evaluates());
    let refused = host
        .os_find_proxy(&source, "http://127.0.0.1:9/x", Duration::from_secs(1))
        .expect_err("a default fake has no OS evaluator");
    assert_eq!(refused.code, PacCode::Fetch);
    assert_eq!(fake.os_finished(), 0);

    // OS mode: the queue answers in order; a blocking answer ends when the test drops its Sender.
    let direct = OsProxyEntry {
        kind: OsProxyKind::Direct,
        host: String::new(),
        port: 0,
    };
    let script_error = PacError {
        code: PacCode::Script,
        detail: "bad script".to_owned(),
    };
    let (tx, rx) = mpsc::channel::<()>();
    let (os_fake, os_handle) = FakePacHost::new()
        .my_ip(IpAddr::V4(Ipv4Addr::new(198, 51, 100, 1)))
        .os_mode()
        .os_answer(OsAnswer::Entries(vec![direct.clone()]))
        .os_answer(OsAnswer::Fail(script_error.clone()))
        .os_answer(OsAnswer::Block(rx))
        .handle();
    let os_host = os_handle.get();
    assert!(os_host.os_evaluates());
    assert_eq!(os_host.my_ip(), IpAddr::V4(Ipv4Addr::new(198, 51, 100, 1)));
    let wait = Duration::from_secs(3);
    assert_eq!(
        os_host.os_find_proxy(&source, "http://127.0.0.1:9/one", wait),
        Ok(vec![direct])
    );
    assert_eq!(
        os_host.os_find_proxy(&source, "http://127.0.0.1:9/two", wait),
        Err(script_error)
    );
    assert_eq!(os_fake.os_finished(), 2);
    let blocked = {
        let h = os_handle.clone();
        let s = source.clone();
        thread::spawn(move || h.get().os_find_proxy(&s, "http://127.0.0.1:9/three", wait))
    };
    assert_eq!(
        os_fake.os_finished(),
        2,
        "the blocking answer returned early"
    );
    let dropped = Instant::now();
    drop(tx);
    let answer = blocked.join().expect("the blocked call does not panic");
    assert!(
        dropped.elapsed() < Duration::from_secs(1),
        "the blocked call outlived its Sender by {:?}",
        dropped.elapsed()
    );
    assert_eq!(
        answer.expect_err("a released block fails").code,
        PacCode::Fetch
    );
    assert_eq!(os_fake.os_finished(), 3);
    let empty = os_host
        .os_find_proxy(&source, "http://127.0.0.1:9/four", wait)
        .expect_err("an empty queue fails");
    assert_eq!(empty.code, PacCode::Fetch);
    assert_eq!(os_fake.os_finished(), 4);
    assert_eq!(
        os_fake.os_calls(),
        [
            ("http://127.0.0.1:9/one".to_owned(), wait),
            ("http://127.0.0.1:9/two".to_owned(), wait),
            ("http://127.0.0.1:9/three".to_owned(), wait),
            ("http://127.0.0.1:9/four".to_owned(), wait),
        ]
    );
    assert_eq!(
        fake.os_calls(),
        [("http://127.0.0.1:9/x".to_owned(), Duration::from_secs(1))]
    );

    // Identity equality and the opaque Debug.
    assert_eq!(handle.clone(), handle);
    assert_ne!(handle, os_handle);
    assert_eq!(format!("{handle:?}"), "PacHostHandle");
}

// ---- shared helpers of the tests below ------------------------------------------------------------------------

fn t(s: &str) -> Arg {
    Arg::Text(s.to_owned())
}

fn n(x: f64) -> Arg {
    Arg::Num(x)
}

/// A broken-down date literal (`weekday` 0 = Sunday).
fn at(year: i64, month: u32, day: u32, weekday: u32, hour: u32, minute: u32, second: u32) -> Civil {
    Civil {
        year,
        month,
        day,
        weekday,
        hour,
        minute,
        second,
    }
}

fn proxy(url: &str) -> ProxyChoice {
    ProxyChoice::Proxy(ProxyConfig {
        url: url.to_owned(),
        no_proxy: Vec::new(),
    })
}

/// The calling thread's captured `warn!` messages (the capture logger is per thread, FR-088).
fn warns() -> Vec<String> {
    take_mine()
        .into_iter()
        .filter(|(_, level, _)| *level == log::Level::Warn)
        .map(|(_, _, message)| message)
        .collect()
}

fn ipv4(a: u8, b: u8, c: u8, d: u8) -> Ipv4Addr {
    Ipv4Addr::new(a, b, c, d)
}

// ---- every host -----------------------------------------------------------------------------------------------

/// SC-033 (plan §7.1 row SC-033): the pure helpers; expected values from MDN's PAC page (plan §1.2), reversed ranges
/// matching only their endpoints (SI-35), and the cost-bound rows (plan §4.15).
#[test]
fn pac_helper_cases() {
    use helpers::*;

    // isPlainHostName (spec SC-033) and dnsDomainIs / localHostOrDomainIs / dnsDomainLevels (MDN's examples).
    assert_eq!(is_plain_host_name("intranet"), Some(true));
    assert_eq!(is_plain_host_name("a.b"), Some(false));
    assert_eq!(dns_domain_is("www.mozilla.org", ".mozilla.org"), Some(true));
    assert_eq!(dns_domain_is("www", ".mozilla.org"), Some(false));
    assert_eq!(
        local_host_or_domain_is("www.mozilla.org", "www.mozilla.org"),
        Some(true)
    );
    assert_eq!(
        local_host_or_domain_is("www", "www.mozilla.org"),
        Some(true)
    );
    assert_eq!(
        local_host_or_domain_is("www.google.com", "www.mozilla.org"),
        Some(false)
    );
    assert_eq!(
        local_host_or_domain_is("home.mozilla.org", "www.mozilla.org"),
        Some(false)
    );
    assert_eq!(dns_domain_levels("www"), Some(0));
    assert_eq!(dns_domain_levels("mozilla.org"), Some(1));
    assert_eq!(dns_domain_levels("www.mozilla.org"), Some(2));

    // shExpMatch: MDN's examples, `*` and `?`, and the algorithm-sanity rows.
    assert_eq!(
        sh_exp_match("http://home.netscape.com/people/ari/index.html", "*/ari/*"),
        Some(true)
    );
    assert_eq!(
        sh_exp_match(
            "http://home.netscape.com/people/montulli/index.html",
            "*/ari/*"
        ),
        Some(false)
    );
    for (s, pat) in [("abc", "a?c"), ("abc", "a*c"), ("abc", "*"), ("", "*")] {
        assert_eq!(sh_exp_match(s, pat), Some(true), "{s:?} vs {pat:?}");
    }
    for (s, pat) in [("abc", "a*d"), ("", "?"), ("abc", "ab"), ("ab", "abc")] {
        assert_eq!(sh_exp_match(s, pat), Some(false), "{s:?} vs {pat:?}");
    }

    // The cost bound: the iterative matcher over the caps (a recursive backtracker would not finish).
    let subject = "a".repeat(MAX_SHEXP_SUBJECT);
    let twenty_stars = format!("{}*b", "*a".repeat(20));
    let cap_pattern = format!("{}*b", "*a".repeat(511));
    assert_eq!(cap_pattern.len(), MAX_SHEXP_PATTERN);
    let start = Instant::now();
    assert_eq!(sh_exp_match(&subject, &twenty_stars), Some(false));
    assert_eq!(sh_exp_match(&subject, &cap_pattern), Some(false));
    assert_eq!(sh_exp_match(&subject, "*a*"), Some(true));
    assert!(
        start.elapsed() < Duration::from_secs(2),
        "the capped matcher took {:?}",
        start.elapsed()
    );

    // The input caps: one byte over → None, exactly at the cap → Some(_).
    let over_host = "h".repeat(MAX_HOST_ARG + 1);
    let at_host = "h".repeat(MAX_HOST_ARG);
    assert_eq!(sh_exp_match(&"a".repeat(MAX_SHEXP_SUBJECT + 1), "*"), None);
    assert_eq!(sh_exp_match("a", &"x".repeat(MAX_SHEXP_PATTERN + 1)), None);
    assert_eq!(sh_exp_match(&subject, "*"), Some(true));
    assert_eq!(
        sh_exp_match("a", &"x".repeat(MAX_SHEXP_PATTERN)),
        Some(false)
    );
    assert_eq!(dns_domain_is(&over_host, ".x"), None);
    assert_eq!(dns_domain_is("a.x", &over_host), None);
    assert_eq!(local_host_or_domain_is(&over_host, "h"), None);
    assert_eq!(local_host_or_domain_is("h", &over_host), None);
    assert_eq!(is_plain_host_name(&over_host), None);
    assert_eq!(dns_domain_levels(&over_host), None);
    assert_eq!(dns_domain_is(&at_host, ".x"), Some(false));
    assert_eq!(local_host_or_domain_is(&at_host, &at_host), Some(true));
    assert_eq!(is_plain_host_name(&at_host), Some(true));
    assert_eq!(dns_domain_levels(&at_host), Some(0));

    // isInNet over literals (the spec rows) and MDN's single-host mask.
    let class_a = (ipv4(10, 0, 0, 0), ipv4(255, 0, 0, 0));
    assert!(is_in_net(ipv4(10, 1, 2, 3), class_a.0, class_a.1));
    assert!(!is_in_net(ipv4(11, 1, 2, 3), class_a.0, class_a.1));
    assert!(is_in_net(
        ipv4(192, 0, 2, 172),
        ipv4(192, 0, 2, 172),
        ipv4(255, 255, 255, 255)
    ));

    // dnsResolve / isResolvable / isInNet by name / myIpAddress through a FakePacHost.
    let wait = Duration::from_secs(1);
    let (fake, handle) = FakePacHost::new()
        .name("ten.test", Some(IpAddr::V4(ipv4(10, 1, 2, 3))))
        .name("eleven.test", Some(IpAddr::V4(ipv4(11, 1, 2, 3))))
        .name(
            "v6only.test",
            Some(IpAddr::V6(std::net::Ipv6Addr::LOCALHOST)),
        )
        .name("gone.test", None)
        .handle();
    let host = handle.get();
    assert_eq!(dns_resolve(host, "ten.test", wait), Some(ipv4(10, 1, 2, 3)));
    let in_class_a = |name: &str| {
        dns_resolve(host, name, wait).is_some_and(|ip| is_in_net(ip, class_a.0, class_a.1))
    };
    assert!(in_class_a("ten.test"));
    assert!(!in_class_a("eleven.test"));
    assert!(dns_resolve(host, "eleven.test", wait).is_some());
    assert_eq!(dns_resolve(host, "gone.test", wait), None);
    assert_eq!(dns_resolve(host, "v6only.test", wait), None);
    // An IP literal is parsed without a lookup.
    assert_eq!(
        dns_resolve(host, "192.0.2.1", wait),
        Some(ipv4(192, 0, 2, 1))
    );
    assert_eq!(dns_resolve(host, "::1", wait), None);
    let names: Vec<String> = fake.resolve_log().into_iter().map(|(n, _)| n).collect();
    assert_eq!(
        names,
        [
            "ten.test",
            "ten.test",
            "eleven.test",
            "eleven.test",
            "gone.test",
            "v6only.test"
        ]
    );
    assert_eq!(my_ip_address(host), ipv4(192, 0, 2, 7));
    let (_, v6_handle) = FakePacHost::new()
        .my_ip(IpAddr::V6(std::net::Ipv6Addr::LOCALHOST))
        .handle();
    assert_eq!(my_ip_address(v6_handle.get()), Ipv4Addr::LOCALHOST);

    // weekdayRange (MDN's examples; FRI..MON reversed → Friday and Monday only).
    let wed = at(2026, 10, 28, 3, 12, 0, 0);
    let fri = at(2026, 10, 30, 5, 12, 0, 0);
    let sat = at(2026, 10, 31, 6, 12, 0, 0);
    let mon = at(2026, 11, 2, 1, 12, 0, 0);
    assert_eq!(weekday_range(&fri, &[t("MON"), t("FRI")]), Some(true));
    assert_eq!(weekday_range(&sat, &[t("MON"), t("FRI")]), Some(false));
    assert_eq!(
        weekday_range(&wed, &[t("MON"), t("FRI"), t("GMT")]),
        Some(true)
    );
    assert_eq!(weekday_range(&sat, &[t("SAT")]), Some(true));
    assert_eq!(weekday_range(&fri, &[t("SAT")]), Some(false));
    assert_eq!(weekday_range(&sat, &[t("SAT"), t("GMT")]), Some(true));
    for (day, expected) in [(&fri, true), (&mon, true), (&wed, false), (&sat, false)] {
        assert_eq!(
            weekday_range(day, &[t("FRI"), t("MON")]),
            Some(expected),
            "FRI..MON on weekday {}",
            day.weekday
        );
    }
    assert_eq!(weekday_range(&fri, &[]), None);
    assert_eq!(weekday_range(&fri, &[t("FRIDAY")]), None);
    assert_eq!(weekday_range(&fri, &[n(5.0)]), None);
    assert_eq!(weekday_range(&fri, &[t("MON"), t("TUE"), t("WED")]), None);

    // dateRange (MDN's examples).
    let day = |y: i64, m: u32, d: u32| at(y, m, d, 0, 12, 0, 0);
    assert_eq!(date_range(&day(2026, 10, 1), &[n(1.0)]), Some(true));
    assert_eq!(date_range(&day(2026, 10, 2), &[n(1.0)]), Some(false));
    assert_eq!(
        date_range(&day(2026, 10, 1), &[n(1.0), t("GMT")]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 10, 15), &[n(1.0), n(15.0)]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 10, 16), &[n(1.0), n(15.0)]),
        Some(false)
    );
    assert_eq!(
        date_range(&day(2026, 12, 24), &[n(24.0), t("DEC")]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 12, 25), &[n(24.0), t("DEC")]),
        Some(false)
    );
    assert_eq!(
        date_range(&day(2026, 11, 24), &[n(24.0), t("DEC")]),
        Some(false)
    );
    assert_eq!(
        date_range(&day(2026, 2, 10), &[t("JAN"), t("MAR")]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 4, 1), &[t("JAN"), t("MAR")]),
        Some(false)
    );
    let summer = [n(1.0), t("JUN"), n(15.0), t("AUG")];
    assert_eq!(date_range(&day(2026, 6, 1), &summer), Some(true));
    assert_eq!(date_range(&day(2026, 8, 15), &summer), Some(true));
    assert_eq!(date_range(&day(2026, 8, 16), &summer), Some(false));
    assert_eq!(date_range(&day(2026, 5, 31), &summer), Some(false));
    let summer_1995 = [n(1.0), t("JUN"), n(1995.0), n(15.0), t("AUG"), n(1995.0)];
    assert_eq!(date_range(&day(1995, 7, 1), &summer_1995), Some(true));
    assert_eq!(date_range(&day(1996, 7, 1), &summer_1995), Some(false));
    let winter = [t("OCT"), n(1995.0), t("MAR"), n(1996.0)];
    assert_eq!(date_range(&day(1995, 10, 1), &winter), Some(true));
    assert_eq!(date_range(&day(1996, 3, 31), &winter), Some(true));
    assert_eq!(date_range(&day(1996, 4, 1), &winter), Some(false));
    assert_eq!(date_range(&day(1995, 9, 30), &winter), Some(false));
    // One number above 31 is a year (FR-064), with or without "GMT".
    assert_eq!(date_range(&day(1995, 3, 3), &[n(1995.0)]), Some(true));
    assert_eq!(date_range(&day(1996, 3, 3), &[n(1995.0)]), Some(false));
    assert_eq!(
        date_range(&day(1995, 3, 3), &[n(1995.0), t("GMT")]),
        Some(true)
    );
    assert_eq!(date_range(&day(2026, 3, 3), &[n(32.0)]), Some(false));
    assert_eq!(
        date_range(&day(1996, 1, 1), &[n(1995.0), n(1997.0)]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(1998, 1, 1), &[n(1995.0), n(1997.0)]),
        Some(false)
    );
    // A reversed range matches only its two endpoint dates.
    assert_eq!(
        date_range(&day(2026, 10, 20), &[n(20.0), n(10.0)]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 10, 10), &[n(20.0), n(10.0)]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 10, 15), &[n(20.0), n(10.0)]),
        Some(false)
    );
    assert_eq!(
        date_range(&day(2026, 1, 15), &[t("DEC"), t("JAN")]),
        Some(true)
    );
    assert_eq!(
        date_range(&day(2026, 6, 15), &[t("DEC"), t("JAN")]),
        Some(false)
    );
    // Bad arguments → None (the engine throws).
    assert_eq!(date_range(&day(2026, 1, 1), &[]), None);
    assert_eq!(date_range(&day(2026, 1, 1), &[n(0.0)]), None);
    assert_eq!(date_range(&day(2026, 1, 1), &[n(1.5)]), None);
    assert_eq!(date_range(&day(2026, 1, 1), &[t("XYZ")]), None);
    assert_eq!(
        date_range(&day(2026, 1, 1), &[n(1.0), n(2.0), n(3.0)]),
        None
    );

    // timeRange (MDN's examples; 23..0 reversed → hours 23 and 0 only).
    let time = |h: u32, m: u32, s: u32| at(2026, 10, 31, 6, h, m, s);
    assert_eq!(time_range(&time(12, 30, 0), &[n(12.0)]), Some(true));
    assert_eq!(time_range(&time(13, 0, 0), &[n(12.0)]), Some(false));
    assert_eq!(
        time_range(&time(12, 30, 0), &[n(12.0), t("GMT")]),
        Some(true)
    );
    let noon_to_one = [n(12.0), n(13.0)];
    assert_eq!(time_range(&time(12, 0, 0), &noon_to_one), Some(true));
    assert_eq!(time_range(&time(12, 59, 59), &noon_to_one), Some(true));
    assert_eq!(time_range(&time(13, 0, 0), &noon_to_one), Some(true));
    assert_eq!(time_range(&time(13, 0, 1), &noon_to_one), Some(false));
    assert_eq!(time_range(&time(11, 59, 59), &noon_to_one), Some(false));
    assert_eq!(time_range(&time(16, 59, 0), &[n(9.0), n(17.0)]), Some(true));
    let office = [n(8.0), n(30.0), n(17.0), n(0.0)];
    assert_eq!(time_range(&time(8, 29, 59), &office), Some(false));
    assert_eq!(time_range(&time(8, 30, 0), &office), Some(true));
    assert_eq!(time_range(&time(17, 0, 0), &office), Some(true));
    assert_eq!(time_range(&time(17, 1, 0), &office), Some(false));
    let half_minute = [n(0.0), n(0.0), n(0.0), n(0.0), n(0.0), n(30.0)];
    assert_eq!(time_range(&time(0, 0, 30), &half_minute), Some(true));
    assert_eq!(time_range(&time(0, 0, 31), &half_minute), Some(false));
    let late = [n(23.0), n(0.0)];
    assert_eq!(time_range(&time(23, 30, 0), &late), Some(true));
    assert_eq!(time_range(&time(0, 30, 0), &late), Some(true));
    assert_eq!(time_range(&time(12, 0, 0), &late), Some(false));
    assert_eq!(time_range(&time(12, 0, 0), &[]), None);
    assert_eq!(time_range(&time(12, 0, 0), &[n(24.0)]), None);
    assert_eq!(time_range(&time(12, 0, 0), &[n(1.0), n(2.0), n(3.0)]), None);
    assert_eq!(time_range(&time(12, 0, 0), &[t("NOON")]), None);

    // "GMT" vs local over clock_civil (2023-11-14 22:13:20 UTC, a Tuesday, at +01:00).
    let clock = PacClock {
        unix_secs: 1_700_000_000,
        local_offset_secs: Some(3600),
    };
    let mut warned = false;
    let local = clock_civil(clock, false, &mut warned);
    let utc = clock_civil(clock, true, &mut warned);
    assert_eq!(local, at(2023, 11, 14, 2, 23, 13, 20));
    assert_eq!(utc, at(2023, 11, 14, 2, 22, 13, 20));
    assert_eq!(time_range(&local, &[n(23.0)]), Some(true));
    assert_eq!(time_range(&utc, &[n(23.0), t("GMT")]), Some(false));
    assert_eq!(time_range(&utc, &[n(22.0), t("GMT")]), Some(true));
    assert_eq!(weekday_range(&local, &[t("TUE")]), Some(true));
    assert!(!warned, "a known offset never warns");
}

/// SC-033 `find_proxy_result_cases` (FR-067, SI-29): the answer parser.
#[test]
fn find_proxy_result_cases() {
    let ok = |answer: &str| parse_answer(answer).expect("a usable answer");
    let a1 = proxy("http://a:1");

    assert_eq!(
        ok("PROXY a:1; DIRECT"),
        (vec![a1.clone(), ProxyChoice::Direct], vec![])
    );
    assert_eq!(
        ok("proxy a:1;direct"),
        (vec![a1.clone(), ProxyChoice::Direct], vec![])
    );
    assert_eq!(ok("HTTP a:1").0, [a1]);
    assert_eq!(ok("HTTPS p:3128").0, [proxy("https://p:3128")]);
    assert_eq!(ok("HTTPS p").0, [proxy("https://p:443")]);
    assert_eq!(ok("SOCKS s").0, [proxy("socks4://s:1080")]);
    assert_eq!(ok("SOCKS4 s:2").0, [proxy("socks4://s:2")]);
    assert_eq!(ok("SOCKS5 s:3").0, [proxy("socks5://s:3")]);
    assert_eq!(ok("SOCKS5 s").0, [proxy("socks5://s:1080")]);
    // Empty entries are skipped silently.
    assert_eq!(
        ok(" PROXY a ; ; DIRECT"),
        (vec![proxy("http://a:80"), ProxyChoice::Direct], vec![])
    );
    assert_eq!(ok("PROXY [::1]:3128").0, [proxy("http://[::1]:3128")]);
    assert_eq!(ok("PROXY [::1]").0, [proxy("http://[::1]:80")]);

    // Each unusable entry: skipped with exactly one E_PAC_RESULT warning naming it.
    for bad in [
        "BOGUS x",
        "PROXY a:0",
        "PROXY a:65536",
        "PROXY a:",
        "PROXY a:x1",
        "PROXY",
        "PROXY ::1:3128",
        "PROXY [::1",
        "PROXY [nope]:1",
        "PROXY a:1 b:2",
        "PROXY u@a:1",
        "PROXY a/b:1",
        "DIRECT x",
    ] {
        let (choices, warnings) = ok(&format!("{bad}; DIRECT"));
        assert_eq!(choices, [ProxyChoice::Direct], "{bad:?}");
        assert_eq!(warnings.len(), 1, "{bad:?}: {warnings:?}");
        assert!(
            warnings[0].starts_with("E_PAC_RESULT") && warnings[0].contains(bad),
            "{bad:?}: {warnings:?}"
        );
    }

    // No usable entry, or an over-long answer: E_PAC_RESULT.
    for unusable in ["", " ; ;", "BOGUS x", "PROXY a:0; NOPE"] {
        let error = parse_answer(unusable).expect_err(unusable);
        assert_eq!(error.code, PacCode::Result, "{unusable:?}");
    }
    let at_cap = format!("DIRECT{}", " ".repeat(MAX_PAC_ANSWER_BYTES - 6));
    assert_eq!(at_cap.len(), MAX_PAC_ANSWER_BYTES);
    assert_eq!(ok(&at_cap).0, [ProxyChoice::Direct]);
    let over_cap = format!("{at_cap} ");
    assert_eq!(
        parse_answer(&over_cap).expect_err("over the cap").code,
        PacCode::Result
    );

    // 20 usable entries: the first 16 kept, one warning.
    let twenty = vec!["DIRECT"; 20].join("; ");
    let (choices, warnings) = ok(&twenty);
    assert_eq!(choices, vec![ProxyChoice::Direct; MAX_PAC_ENTRIES]);
    assert_eq!(warnings.len(), 1, "{warnings:?}");
    assert!(warnings[0].starts_with("E_PAC_RESULT"));
    // Exactly 16 entries: no warning.
    assert_eq!(ok(&vec!["DIRECT"; 16].join("; ")).1, Vec::<String>::new());
}

/// SC-035 `wpad_candidate_cases` (FR-066, PD-30).
#[test]
fn wpad_candidate_cases() {
    assert_eq!(WPAD_DNS_URL, "http://wpad/wpad.dat");
    let parsed = url::Url::parse(WPAD_DNS_URL).expect("WPAD_DNS_URL parses");
    crate::http::check_pac_url(crate::http::UrlPolicy::HttpsOnly, &parsed)
        .expect("the WPAD candidate passes the PAC URL rule");
    assert_eq!(parsed.host_str(), Some("wpad"));
    // A function of no host name: no devolved candidate, no DHCP.
    assert_eq!(wpad_dns_candidates(), vec![WPAD_DNS_URL.to_owned()]);
    assert!(InertPacHost.wpad_candidates(PAC_PHASE_BUDGET).is_empty());
}

/// SC-041 `winhttp_answer_cases` (SI-38, PD-36): `os_answer` then `parse_answer`.
#[test]
fn winhttp_answer_cases() {
    fn entry(kind: OsProxyKind, host: &str, port: u16) -> OsProxyEntry {
        OsProxyEntry {
            kind,
            host: host.to_owned(),
            port,
        }
    }
    let direct = entry(OsProxyKind::Direct, "", 0);
    let walk = |entries: &[OsProxyEntry]| {
        let (text, mut warnings) = os_answer(entries);
        parse_answer(&text).map(|(choices, more)| {
            warnings.extend(more);
            (choices, warnings)
        })
    };
    let ok = |entries: &[OsProxyEntry]| walk(entries).expect("a usable OS answer");

    assert_eq!(
        ok(std::slice::from_ref(&direct)),
        (vec![ProxyChoice::Direct], vec![])
    );
    assert_eq!(
        ok(&[
            entry(OsProxyKind::Http, "a", 8080),
            entry(OsProxyKind::Http, "b", 3128)
        ])
        .0,
        [proxy("http://a:8080"), proxy("http://b:3128")]
    );
    let mixed = [entry(OsProxyKind::Http, "a", 8080), direct.clone()];
    assert_eq!(os_answer(&mixed).0, "PROXY a:8080; DIRECT");
    assert_eq!(ok(&mixed).0, [proxy("http://a:8080"), ProxyChoice::Direct]);
    assert_eq!(
        ok(&[entry(OsProxyKind::Https, "p", 3128)]).0,
        [proxy("https://p:3128")]
    );
    assert_eq!(
        ok(&[entry(OsProxyKind::Socks, "s", 1080)]).0,
        [proxy("socks4://s:1080")]
    );
    assert_eq!(
        ok(&[entry(OsProxyKind::Http, "a", 0)]).0,
        [proxy("http://a:80")]
    );
    assert_eq!(
        ok(&[entry(OsProxyKind::Http, "::1", 3128)]).0,
        [proxy("http://[::1]:3128")]
    );

    // Each unusable entry: skipped with one E_PAC_RESULT warning.
    for skipped in [
        entry(OsProxyKind::Other(3), "ftp", 21),
        entry(OsProxyKind::Http, "", 8080),
        entry(OsProxyKind::Http, "a;b", 1),
        entry(OsProxyKind::Http, "a b", 1),
        entry(OsProxyKind::Http, "a\nb", 1),
    ] {
        let (choices, warnings) = ok(&[skipped.clone(), direct.clone()]);
        assert_eq!(choices, [ProxyChoice::Direct], "{skipped:?}");
        assert_eq!(warnings.len(), 1, "{skipped:?}: {warnings:?}");
        assert!(warnings[0].starts_with("E_PAC_RESULT"), "{warnings:?}");
        assert!(!warnings[0].contains('\n'), "{warnings:?}");
    }

    // Nothing usable: E_PAC_RESULT.
    assert_eq!(walk(&[]).expect_err("empty").code, PacCode::Result);
    assert_eq!(
        walk(&[entry(OsProxyKind::Other(3), "ftp", 21)])
            .expect_err("only an unknown scheme")
            .code,
        PacCode::Result
    );

    // 20 Direct: 16 kept, one warning.
    let (choices, warnings) = ok(&vec![direct; 20]);
    assert_eq!(choices.len(), MAX_PAC_ENTRIES);
    assert_eq!(warnings.len(), 1, "{warnings:?}");
}

/// SC-043 `pac_clock_cases` (FR-095, PD-31).
#[test]
fn pac_clock_cases() {
    use helpers::{civil, time_range, weekday_range};
    install_capture();
    take_mine();

    // 2026-10-31 23:30:00 UTC, a Saturday, at +02:00 (local: Sunday 01:30).
    let clock = PacClock {
        unix_secs: 1_793_489_400,
        local_offset_secs: Some(7200),
    };
    let mut warned = false;
    let local = clock_civil(clock, false, &mut warned);
    assert_eq!(local, at(2026, 11, 1, 0, 1, 30, 0));
    assert_eq!(weekday_range(&local, &[t("SUN")]), Some(true));
    assert_eq!(time_range(&local, &[n(1.0), n(2.0)]), Some(true));
    let utc = clock_civil(clock, true, &mut warned);
    assert_eq!(utc, at(2026, 10, 31, 6, 23, 30, 0));
    assert_eq!(weekday_range(&utc, &[t("SAT"), t("GMT")]), Some(true));
    assert_eq!(weekday_range(&utc, &[t("SUN"), t("GMT")]), Some(false));
    assert_eq!(time_range(&utc, &[n(1.0), n(2.0), t("GMT")]), Some(false));
    assert!(!warned);
    assert_eq!(warns(), Vec::<String>::new());

    // An unknown offset: the UTC fields, and one warn for two calls sharing one flag.
    let unknown = PacClock {
        local_offset_secs: None,
        ..clock
    };
    let mut warned = false;
    assert_eq!(clock_civil(unknown, false, &mut warned), utc);
    assert_eq!(clock_civil(unknown, false, &mut warned), utc);
    assert!(warned);
    let logged = warns();
    assert_eq!(logged.len(), 1, "{logged:?}");

    // helpers::civil, days-from-civil by hand.
    assert_eq!(civil(0), at(1970, 1, 1, 4, 0, 0, 0));
    assert_eq!(civil(951_782_400), at(2000, 2, 29, 2, 0, 0, 0));
    assert_eq!(civil(4_102_444_800), at(2100, 1, 1, 5, 0, 0, 0));
    assert_eq!(civil(-1), at(1969, 12, 31, 3, 23, 59, 59));
    assert_eq!(civil(-62_135_596_800), at(1, 1, 1, 1, 0, 0, 0));
}

// ---- the engine (macOS and Linux) -----------------------------------------------------------------------------

#[cfg(not(windows))]
fn url(s: &str) -> url::Url {
    url::Url::parse(s).expect("a test URL")
}

#[cfg(not(windows))]
const GOOD_SCRIPT: &str = "function FindProxyForURL(url, host) { return \"DIRECT\"; }";

/// SC-033 `url_argument_cases` (FR-064): the URL and host a script sees.
#[cfg(not(windows))]
#[test]
fn url_argument_cases() {
    let script = r#"
        function FindProxyForURL(url, host) {
            if (url === "http://127.0.0.1:8080/dir/v.json?x=1" && host === "127.0.0.1") return "PROXY http-ok:1";
            if (url === "https://example.test:8443/" && host === "example.test") return "PROXY https-ok:1";
            if (url === "https://example.test/" && host === "example.test") return "PROXY https-default:1";
            return "PROXY unexpected:1";
        }"#;
    let ask = |target: &str| {
        let (_, handle) = FakePacHost::new().handle();
        evaluate(script, &url(target), handle).expect("the script answers")
    };
    assert_eq!(
        ask("http://127.0.0.1:8080/dir/v.json?x=1"),
        [proxy("http://http-ok:1")]
    );
    assert_eq!(
        ask("https://example.test:8443/dir/versions.json?sig=s#f"),
        [proxy("http://https-ok:1")]
    );
    assert_eq!(
        ask("https://example.test/versions.json"),
        [proxy("http://https-default:1")]
    );
    assert_eq!(
        pac_url_argument(&url("https://u:p@example.test:8443/a?b#c")),
        "https://example.test:8443/"
    );
    assert_eq!(
        pac_url_argument(&url("http://127.0.0.1:1/a?b")),
        "http://127.0.0.1:1/a?b"
    );
}

/// SC-033 R-14 rows `script_world_cases` (PD-26) and the engine cost rows (plan §4.15).
#[cfg(not(windows))]
#[test]
fn script_world_cases() {
    install_capture();
    take_mine();
    let decide = |script: &str, clock: PacClock| {
        let (_, handle) = FakePacHost::new().clock(clock).handle();
        evaluate(script, &url("http://127.0.0.1:1/"), handle)
    };
    let utc = |unix_secs: i64| PacClock {
        unix_secs,
        local_offset_secs: Some(0),
    };
    let noon = utc(1_793_361_600);

    // The script's world: none of these names exists.
    let world = r#"
        function FindProxyForURL(url, host) {
            var t = [typeof Date, typeof require, typeof std, typeof os, typeof fetch,
                     typeof setTimeout, typeof print, typeof console];
            for (var i = 0; i < t.length; i++) if (t[i] !== "undefined") return "PROXY leak" + i + ":1";
            return "DIRECT";
        }"#;
    assert_eq!(decide(world, noon), Ok(vec![ProxyChoice::Direct]));

    // alert returns undefined and logs nothing.
    let alert = r#"function FindProxyForURL(url, host) {
        return alert("x") === undefined ? "DIRECT" : "PROXY bad:1"; }"#;
    assert_eq!(decide(alert, noon), Ok(vec![ProxyChoice::Direct]));
    assert_eq!(take_mine(), Vec::new());

    // The IPv6 Ex variants and convert_addr are undefined: E_PAC_SCRIPT.
    for name in [
        "isResolvableEx",
        "dnsResolveEx",
        "isInNetEx",
        "myIpAddressEx",
        "sortIpAddressList",
        "convert_addr",
    ] {
        let script =
            format!("function FindProxyForURL(url, host) {{ {name}(\"a\"); return \"DIRECT\"; }}");
        let error = decide(&script, noon).expect_err(name);
        assert_eq!(error.code, PacCode::Script, "{name}: {error}");
    }

    // Reversed ranges through the FakePacHost clock: endpoints only.
    let wd = r#"function FindProxyForURL(u, h) { return weekdayRange("FRI", "MON") ? "DIRECT" : "PROXY no:1"; }"#;
    for (unix, expected) in [
        (1_793_361_600, true),  // Friday
        (1_793_620_800, true),  // Monday
        (1_793_188_800, false), // Wednesday
        (1_793_448_000, false), // Saturday
    ] {
        let direct = decide(wd, utc(unix)) == Ok(vec![ProxyChoice::Direct]);
        assert_eq!(direct, expected, "weekdayRange at {unix}");
    }
    let tr =
        r#"function FindProxyForURL(u, h) { return timeRange(23, 0) ? "DIRECT" : "PROXY no:1"; }"#;
    for (unix, expected) in [
        (1_793_489_400, true),  // 23:30
        (1_793_493_000, true),  // 00:30
        (1_793_448_000, false), // 12:00
    ] {
        let direct = decide(tr, utc(unix)) == Ok(vec![ProxyChoice::Direct]);
        assert_eq!(direct, expected, "timeRange at {unix}");
    }
    let dr =
        r#"function FindProxyForURL(u, h) { return dateRange(30, 28) ? "DIRECT" : "PROXY no:1"; }"#;
    for (unix, expected) in [
        (1_793_361_600, true),  // the 30th
        (1_793_188_800, true),  // the 28th
        (1_793_275_200, false), // the 29th
    ] {
        let direct = decide(dr, utc(unix)) == Ok(vec![ProxyChoice::Direct]);
        assert_eq!(direct, expected, "dateRange at {unix}");
    }

    // The engine cost rows.
    let capped = r#"function FindProxyForURL(u, h) {
        return shExpMatch('a'.repeat(4096), '*a'.repeat(511) + '*b') ? "PROXY yes:1" : "DIRECT"; }"#;
    let start = Instant::now();
    assert_eq!(decide(capped, noon), Ok(vec![ProxyChoice::Direct]));
    assert!(
        start.elapsed() < PAC_EVAL_BUDGET,
        "the capped shExpMatch call took {:?}",
        start.elapsed()
    );
    for call in [
        "shExpMatch('a'.repeat(1e6), '*a*b')",
        "dnsResolve('h'.repeat(254))",
    ] {
        let (fake, handle) = FakePacHost::new().handle();
        let uncaught =
            format!("function FindProxyForURL(u, h) {{ {call}; return \"PROXY no:1\"; }}");
        let error =
            evaluate(&uncaught, &url("http://127.0.0.1:1/"), handle.clone()).expect_err(call);
        assert_eq!(error.code, PacCode::Script, "{call}: {error}");
        let caught = format!(
            "function FindProxyForURL(u, h) {{ try {{ {call}; }} catch (e) {{ return \"DIRECT\"; }} return \"PROXY no:1\"; }}"
        );
        assert_eq!(
            evaluate(&caught, &url("http://127.0.0.1:1/"), handle),
            Ok(vec![ProxyChoice::Direct]),
            "{call}"
        );
        assert_eq!(fake.resolve_log(), Vec::new(), "{call}");
    }

    // The clock is read once per session (FR-095), and never without a time helper.
    take_mine();
    let (fake, handle) = FakePacHost::new()
        .clock(PacClock {
            unix_secs: 1_793_489_400,
            local_offset_secs: None,
        })
        .handle();
    let timed = r#"function FindProxyForURL(u, h) {
        weekdayRange("SAT"); timeRange(1, 2); dateRange(31); return "DIRECT"; }"#;
    let deadline = Instant::now() + PAC_PHASE_BUDGET;
    let mut session =
        PacSession::open(timed, handle, PacLimits::production(), deadline).expect("compiles");
    for target in ["http://127.0.0.1:1/a", "http://127.0.0.1:2/b"] {
        assert_eq!(
            session.find_proxy(&url(target), deadline),
            Ok(vec![ProxyChoice::Direct])
        );
    }
    assert_eq!(fake.now_calls(), 1);
    let logged = warns();
    assert_eq!(logged.len(), 1, "{logged:?}");
    let (fake, handle) = FakePacHost::new().handle();
    let mut session =
        PacSession::open(GOOD_SCRIPT, handle, PacLimits::production(), deadline).expect("compiles");
    assert_eq!(
        session.find_proxy(&url("http://127.0.0.1:1/"), deadline),
        Ok(vec![ProxyChoice::Direct])
    );
    assert_eq!(fake.now_calls(), 0);
}

/// SC-036 `script_failure_cases` (FR-068, SI-28) and the log escaping rows (plan §4.15).
#[cfg(not(windows))]
#[test]
fn script_failure_cases() {
    install_capture();
    take_mine();
    let target = url("http://127.0.0.1:1/");
    let run = |script: &str| {
        let (_, handle) = FakePacHost::new().handle();
        let start = Instant::now();
        let outcome = evaluate(script, &target, handle);
        (outcome, start.elapsed())
    };
    let cases: [(&str, &str, PacCode); 15] = [
        (
            "syntax",
            "function FindProxyForURL(u, h) {",
            PacCode::Script,
        ),
        (
            "top-level throw",
            "throw new Error('boom');",
            PacCode::Script,
        ),
        (
            "call throws",
            "function FindProxyForURL(u, h) { throw new Error('boom'); }",
            PacCode::Script,
        ),
        ("no FindProxyForURL", "var x = 1;", PacCode::Script),
        (
            "not a function",
            "var FindProxyForURL = 'DIRECT';",
            PacCode::Script,
        ),
        (
            "undefined",
            "function FindProxyForURL(u, h) { return undefined; }",
            PacCode::Result,
        ),
        (
            "number",
            "function FindProxyForURL(u, h) { return 42; }",
            PacCode::Result,
        ),
        (
            "object",
            "function FindProxyForURL(u, h) { return {}; }",
            PacCode::Result,
        ),
        (
            "null",
            "function FindProxyForURL(u, h) { return null; }",
            PacCode::Result,
        ),
        (
            "loop",
            "function FindProxyForURL(){ while (true) {} }",
            PacCode::Script,
        ),
        ("top-level loop", "while (true) {}", PacCode::Script),
        ("stack", "function f(){ f() } f()", PacCode::Script),
        (
            "uncatchable interrupt",
            "function FindProxyForURL(){ try { while(true){} } catch(e) {} return 'DIRECT'; }",
            PacCode::Script,
        ),
        (
            "regex",
            "function FindProxyForURL(){ /^(a+)+$/.test('a'.repeat(40) + 'b'); return 'DIRECT'; }",
            PacCode::Script,
        ),
        (
            "heap",
            "function FindProxyForURL(){ var a=[]; while(true) a.push('x'.repeat(1e5)); }",
            PacCode::Script,
        ),
    ];
    for (name, script, code) in cases {
        let (outcome, took) = run(script);
        let error = outcome.expect_err(name);
        assert_eq!(error.code, code, "{name}: {error}");
        assert!(
            took < PAC_EVAL_BUDGET + Duration::from_secs(1),
            "{name} took {took:?}"
        );
        eprintln!("script_failure_cases {name}: {took:?} ({error})");
        // The process is alive and the thread can evaluate again.
        assert_eq!(
            run(GOOD_SCRIPT).0,
            Ok(vec![ProxyChoice::Direct]),
            "after {name}"
        );
    }
    let (too_big, _) = run(&format!("{GOOD_SCRIPT}//{}", "x".repeat(1024 * 1024)));
    assert_eq!(
        too_big.expect_err("over MAX_PAC_BYTES").code,
        PacCode::Script
    );

    // Log escaping: script text reaches the log only through log_text.
    take_mine();
    let thrown = run(r#"function FindProxyForURL(){ throw "a\nb\rc"; }"#)
        .0
        .expect_err("throws");
    assert_eq!(thrown.code, PacCode::Script);
    assert!(
        !thrown.detail.contains(['\n', '\r']) && thrown.detail.contains("\\n"),
        "{:?}",
        thrown.detail
    );
    let answered = run(r#"function FindProxyForURL(){ return "PROXY x\ny; DIRECT"; }"#).0;
    assert_eq!(answered, Ok(vec![ProxyChoice::Direct]));
    let logged = warns();
    assert_eq!(logged.len(), 1, "{logged:?}");
    for message in &logged {
        assert!(!message.contains(['\n', '\r']), "{message:?}");
    }
    assert!(logged[0].contains("\\n"), "{logged:?}");
    let long = run(r#"function FindProxyForURL(){ throw "é".repeat(300); }"#)
        .0
        .expect_err("throws");
    assert!(long.detail.ends_with('…'), "{:?}", long.detail);
    assert_eq!(long.detail.matches('é').count(), 200, "{:?}", long.detail);
    assert_eq!(log_text(&"é".repeat(300)), format!("{}…", "é".repeat(200)));
    assert_eq!(log_text("short"), "short");
}

/// SC-042 `dns_bound_cases` (FR-094, PD-29, TA-22).
#[cfg(not(windows))]
#[test]
fn dns_bound_cases() {
    install_capture();
    take_mine();
    let (fake, handle) = FakePacHost::new()
        .hang("slow.test")
        .hang("slow2.test")
        .name("a.test", Some(IpAddr::V4(ipv4(10, 0, 0, 1))))
        .name("gone.test", None)
        .handle();
    let host = handle.get();
    let mut memo = DnsMemo::new();
    let deadline = Instant::now() + Duration::from_secs(30);

    // A hanging name: unresolvable after PAC_DNS_WAIT, its worker not killed; the next name still resolves.
    let start = Instant::now();
    assert_eq!(memo.lookup(host, "slow.test", deadline), None);
    let took = start.elapsed();
    eprintln!("dns_bound_cases hanging lookup: {took:?}");
    assert!(took < PAC_DNS_WAIT + Duration::from_secs(1), "{took:?}");
    assert_eq!(fake.hangs_finished(), 0);
    assert_eq!(
        memo.lookup(host, "a.test", deadline),
        Some(ipv4(10, 0, 0, 1))
    );
    // Memoised, a failed answer included; an IP literal needs no lookup.
    assert_eq!(memo.lookup(host, "slow.test", deadline), None);
    assert_eq!(memo.lookup(host, "gone.test", deadline), None);
    assert_eq!(memo.lookup(host, "gone.test", deadline), None);
    assert_eq!(
        memo.lookup(host, "a.test", deadline),
        Some(ipv4(10, 0, 0, 1))
    );
    assert_eq!(
        memo.lookup(host, "192.0.2.1", deadline),
        Some(ipv4(192, 0, 2, 1))
    );
    assert_eq!(memo.lookup(host, "::1", deadline), None);
    // The remaining budget bounds the wait.
    let short = Instant::now() + Duration::from_millis(300);
    let start = Instant::now();
    assert_eq!(memo.lookup(host, "slow2.test", short), None);
    assert!(
        start.elapsed() < Duration::from_secs(1),
        "{:?}",
        start.elapsed()
    );
    let log = fake.resolve_log();
    let names: Vec<&str> = log.iter().map(|(n, _)| n.as_str()).collect();
    assert_eq!(names, ["slow.test", "a.test", "gone.test", "slow2.test"]);
    for (name, wait) in &log {
        assert!(*wait <= PAC_DNS_WAIT, "{name}: {wait:?}");
    }
    assert!(log[3].1 <= Duration::from_millis(300), "{:?}", log[3]);
    fake.release_hangs();
    assert!(
        eventually(Duration::from_secs(2), || fake.hangs_finished() == 2),
        "the abandoned workers did not end on their own: {}",
        fake.hangs_finished()
    );
    assert_eq!(warns(), Vec::<String>::new());

    // The 17th distinct name: unresolvable with no lookup, exactly one warn.
    let mut many = FakePacHost::new();
    for i in 0..=PAC_MAX_LOOKUPS + 1 {
        many = many.name(&format!("n{i}.test"), Some(IpAddr::V4(ipv4(10, 0, 0, 2))));
    }
    let (fake, handle) = many.handle();
    let mut memo = DnsMemo::new();
    for i in 0..PAC_MAX_LOOKUPS {
        assert!(
            memo.lookup(handle.get(), &format!("n{i}.test"), deadline)
                .is_some()
        );
    }
    assert_eq!(fake.resolve_log().len(), PAC_MAX_LOOKUPS);
    assert_eq!(memo.lookup(handle.get(), "n16.test", deadline), None);
    assert_eq!(memo.lookup(handle.get(), "n17.test", deadline), None);
    assert_eq!(fake.resolve_log().len(), PAC_MAX_LOOKUPS);
    // A memoised name still answers past the cap.
    assert!(memo.lookup(handle.get(), "n0.test", deadline).is_some());
    let logged = warns();
    assert_eq!(logged.len(), 1, "{logged:?}");

    // Through the engine: 20 distinct hanging names end within the evaluation budget.
    let mut hanging = FakePacHost::new();
    for i in 0..20 {
        hanging = hanging.hang(&format!("h{i}.test"));
    }
    let (fake, handle) = hanging.handle();
    let script = r#"function FindProxyForURL(u, h) {
        for (var i = 0; i < 20; i++) dnsResolve("h" + i + ".test");
        return "DIRECT"; }"#;
    let start = Instant::now();
    let outcome = evaluate(script, &url("http://127.0.0.1:1/"), handle);
    let took = start.elapsed();
    eprintln!("dns_bound_cases 20 hanging names: {took:?} ({outcome:?})");
    assert!(took < PAC_EVAL_BUDGET + Duration::from_secs(1), "{took:?}");
    for (name, wait) in fake.resolve_log() {
        assert!(wait <= PAC_DNS_WAIT, "{name}: {wait:?}");
    }
    fake.release_hangs();
}
