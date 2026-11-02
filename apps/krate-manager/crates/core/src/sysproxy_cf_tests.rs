//! sysproxy_cf tests (macOS only; plan §4.6, §7.1 row SC-040): dictionaries built with `from_CFType_pairs`, never
//! the real SystemConfiguration store.

use super::*;
use crate::http::ProxyConfig;
use crate::sysproxy::{SystemProxy, from_macos};
use core_foundation::data::CFData;
use std::collections::BTreeMap;

fn pair(key: &str, value: CFType) -> (CFString, CFType) {
    (CFString::new(key), value)
}

fn text(s: &str) -> CFType {
    CFString::new(s).into_CFType()
}

fn number(n: i32) -> CFType {
    CFNumber::from(n).into_CFType()
}

fn strings(items: &[&str]) -> Vec<String> {
    items.iter().map(|s| (*s).to_owned()).collect()
}

/// The dictionary order is CF's, so pairs compare by key.
fn by_key(pairs: Vec<(String, ScValue)>) -> BTreeMap<String, ScValue> {
    pairs.into_iter().collect()
}

#[test]
fn from_cf_proxies_cases() {
    let exceptions = [
        CFString::new("localhost"),
        CFString::new("intranet.example"),
    ];
    let dict: CFDictionary<CFString, CFType> = CFDictionary::from_CFType_pairs(&[
        pair("HTTPSEnable", number(1)),
        pair("HTTPSProxy", text("proxy.example")),
        pair("HTTPSPort", number(3128)),
        pair(
            "ExceptionsList",
            CFArray::from_CFTypes(&exceptions).into_CFType(),
        ),
        pair(
            "ProxyAutoConfigEnable",
            CFBoolean::true_value().into_CFType(),
        ),
        pair(
            "ProxyAutoConfigURLString",
            text("https://pac.example/proxy.pac"),
        ),
        pair(
            "ProxyAutoDiscoveryEnable",
            CFBoolean::false_value().into_CFType(),
        ),
        // skipped, no panic: a value of another CF type, an array holding a non-string
        pair("BlobValue", CFData::from_buffer(b"x").into_CFType()),
        pair(
            "NumberList",
            CFArray::from_CFTypes(&[CFNumber::from(7_i32)]).into_CFType(),
        ),
    ]);

    let converted = from_cf_proxies(&dict);
    let expected = by_key(vec![
        ("HTTPSEnable".to_owned(), ScValue::Int(1)),
        (
            "HTTPSProxy".to_owned(),
            ScValue::Text("proxy.example".to_owned()),
        ),
        ("HTTPSPort".to_owned(), ScValue::Int(3128)),
        (
            "ExceptionsList".to_owned(),
            ScValue::List(strings(&["localhost", "intranet.example"])),
        ),
        ("ProxyAutoConfigEnable".to_owned(), ScValue::Bool(true)),
        (
            "ProxyAutoConfigURLString".to_owned(),
            ScValue::Text("https://pac.example/proxy.pac".to_owned()),
        ),
        ("ProxyAutoDiscoveryEnable".to_owned(), ScValue::Bool(false)),
    ]);
    assert_eq!(converted.len(), expected.len());
    assert_eq!(by_key(converted.clone()), expected);

    assert_eq!(
        from_macos(&converted),
        SystemProxy {
            proxy: Some(ProxyConfig {
                url: "http://proxy.example:3128".to_owned(),
                no_proxy: strings(&["localhost", "intranet.example"]),
            }),
            pac_url: Some("https://pac.example/proxy.pac".to_owned()),
            auto_detect: false,
        }
    );

    // a missing key is simply absent
    let only_host: CFDictionary<CFString, CFType> =
        CFDictionary::from_CFType_pairs(&[pair("HTTPSProxy", text("proxy.example"))]);
    let converted = from_cf_proxies(&only_host);
    assert_eq!(
        converted,
        vec![(
            "HTTPSProxy".to_owned(),
            ScValue::Text("proxy.example".to_owned())
        )]
    );
    assert!(!converted.iter().any(|(key, _)| key == "HTTPSPort"));

    let empty: CFDictionary<CFString, CFType> = CFDictionary::from_CFType_pairs(&[]);
    assert!(from_cf_proxies(&empty).is_empty());
}
