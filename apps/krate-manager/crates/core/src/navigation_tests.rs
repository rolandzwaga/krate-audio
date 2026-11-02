use super::*;

fn parse(text: &str) -> Url {
    Url::parse(text).expect("test URL")
}

#[test]
fn is_app_origin_cases() {
    let cases: [(&str, &str, bool); 8] = [
        ("tauri://localhost/index.html", "tauri://localhost", true),
        ("http://tauri.localhost/x", "http://tauri.localhost", true),
        ("http://localhost:1420/", DEV_URL, true),
        ("https://krateaudio.com/", "http://tauri.localhost", false),
        (
            "http://tauri.localhost.evil.com/",
            "http://tauri.localhost",
            false,
        ),
        ("http://localhost:1421/", DEV_URL, false),
        ("file:///etc/passwd", "tauri://localhost", false),
        ("tauri://evil/", "tauri://localhost", false),
    ];
    for (target, app, expected) in cases {
        assert_eq!(
            is_app_origin(&parse(target), &parse(app)),
            expected,
            "is_app_origin({target}, {app})"
        );
    }
    // Every foreign host is refused against every app origin, not only the one paired above.
    let foreign = [
        "https://krateaudio.com/",
        "http://tauri.localhost.evil.com/",
        "http://localhost:1421/",
        "file:///etc/passwd",
        "tauri://evil/",
    ];
    for app in [DEV_URL, "http://tauri.localhost", "tauri://localhost"] {
        for target in foreign {
            assert!(
                !is_app_origin(&parse(target), &parse(app)),
                "{target} accepted against {app}"
            );
        }
    }
}

#[test]
fn app_origin_cases() {
    for os in [Os::Windows, Os::Macos, Os::Linux] {
        assert_eq!(
            app_origin(true, os),
            parse(DEV_URL),
            "dev origin for {os:?}"
        );
    }
    assert_eq!(
        app_origin(false, Os::Windows),
        parse("http://tauri.localhost")
    );
    assert_eq!(app_origin(false, Os::Macos), parse("tauri://localhost"));
    assert_eq!(app_origin(false, Os::Linux), parse("tauri://localhost"));
}
