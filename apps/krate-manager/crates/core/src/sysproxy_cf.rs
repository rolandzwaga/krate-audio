//! macOS only: the SystemConfiguration proxy dictionary as `ScValue` pairs and the local UTC offset (R-6, FR-075,
//! PD-31; plan §4.6). The only core file that names `core_foundation`.

use crate::sysproxy::{self, ScValue};
use core_foundation::array::CFArray;
use core_foundation::base::{CFType, TCFType};
use core_foundation::boolean::CFBoolean;
use core_foundation::date::CFDate;
use core_foundation::dictionary::CFDictionary;
use core_foundation::number::CFNumber;
use core_foundation::string::CFString;
use core_foundation::timezone::CFTimeZone;

/// Every entry whose key is a `CFString` and whose value is a `CFBoolean`, a `CFNumber` that fits an `i64`, a
/// `CFString` or a `CFArray` of `CFString`s; any other value is skipped (FR-075). Names no key.
pub fn from_cf_proxies(dict: &CFDictionary<CFString, CFType>) -> Vec<(String, ScValue)> {
    let (keys, values) = dict.get_keys_and_values();
    keys.into_iter()
        .zip(values)
        .filter_map(|(key, value)| {
            // SAFETY: `key` is a non-null key of the live `dict`, a CF object (`kCFTypeDictionaryKeyCallBacks`);
            // the get rule retains it, so the wrapper owns its own reference.
            let key = unsafe { CFType::wrap_under_get_rule(key) };
            // SAFETY: `value` is a non-null value of the live `dict`, a CF object; retained as above.
            let value = unsafe { CFType::wrap_under_get_rule(value) };
            Some((key.downcast::<CFString>()?.to_string(), sc_value(&value)?))
        })
        .collect()
}

/// One dictionary value as an `ScValue`, `None` for any other CF type.
fn sc_value(value: &CFType) -> Option<ScValue> {
    if let Some(flag) = value.downcast::<CFBoolean>() {
        return Some(ScValue::Bool(bool::from(flag)));
    }
    if let Some(number) = value.downcast::<CFNumber>() {
        return number.to_i64().map(ScValue::Int);
    }
    if let Some(text) = value.downcast::<CFString>() {
        return Some(ScValue::Text(text.to_string()));
    }
    let array = value.downcast::<CFArray>()?;
    array
        .iter()
        .map(|item| {
            // SAFETY: an element of a property-list `CFArray` is a non-null CF object, valid while `array` lives;
            // the get rule retains it.
            let element = unsafe { CFType::wrap_under_get_rule(*item) };
            element.downcast::<CFString>().map(|text| text.to_string())
        })
        .collect::<Option<Vec<String>>>()
        .map(ScValue::List)
}

/// The macOS local offset the real PAC host reads (PD-31).
pub fn system_utc_offset() -> Option<i32> {
    sysproxy::cf_offset_secs(CFTimeZone::system().seconds_from_gmt(CFDate::now()))
}

#[cfg(test)]
#[path = "sysproxy_cf_tests.rs"]
mod tests;
