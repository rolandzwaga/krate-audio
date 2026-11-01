//! The Phase 1 C1 contract, interpreted (D2, FR-001 to FR-008; plan §4.2).

use std::collections::{BTreeMap, BTreeSet};
use std::sync::OnceLock;

use base64::Engine as _;
use base64::engine::general_purpose::STANDARD;
use regex::Regex;
use serde_json::{Map, Value};

pub const V2_SCHEMA_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/versions.v2.schema.json");
pub const V1_SCHEMA_TEXT: &str =
    include_str!("../../../../../tools/feed-contract/versions.v1.schema.json");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Schema {
    V1,
    V2,
}

/// One contract finding (FR-003).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Finding {
    /// `"E_SCHEMA"`, `"E_TC_FIELDS"`, …
    pub code: &'static str,
    /// RFC 6901, `""` = root.
    pub instance_path: String,
    /// `Some` for `E_SCHEMA` (`"pattern"`, `"false schema"`, …).
    pub keyword: Option<&'static str>,
    pub message: String,
    /// `Some` only for `E_INSTALL_DUPLICATE` (PD-9).
    pub duplicate: Option<DuplicateSides>,
}

/// Both sides of an install-name duplicate, as full paths (PD-9).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DuplicateSides {
    pub field: &'static str,
    pub first: String,
    pub second: String,
}

/// The code rules' codes (FR-004 to FR-006, FR-008).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RuleCode {
    SignatureText,
    TcFields,
    TcName,
    TcVersion,
    TcPlatform,
    TcAsset,
    InstallValue,
}

impl RuleCode {
    pub fn as_str(self) -> &'static str {
        match self {
            RuleCode::SignatureText => "E_SIGNATURE_TEXT",
            RuleCode::TcFields => "E_TC_FIELDS",
            RuleCode::TcName => "E_TC_NAME",
            RuleCode::TcVersion => "E_TC_VERSION",
            RuleCode::TcPlatform => "E_TC_PLATFORM",
            RuleCode::TcAsset => "E_TC_ASSET",
            RuleCode::InstallValue => "E_INSTALL_VALUE",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum ContractError {
    #[error("E_SCHEMA_UNSUPPORTED: {keyword} at {pointer}")]
    Unsupported { pointer: String, keyword: String },
    #[error("{}: {message}", .code.as_str())]
    Rule { code: RuleCode, message: String },
}

/// A minisign signature file, parsed (FR-005).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MinisigText {
    pub untrusted_comment: String,
    pub key_id: [u8; 8],
    pub signature: [u8; 64],
    pub trusted_comment: String,
    pub global_signature: [u8; 64],
}

/// A trusted comment, parsed (FR-006).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrustedComment {
    pub name: String,
    pub version: String,
    pub platform: String,
    pub asset: String,
}

pub const PLATFORM_KEYS: [&str; 5] = [
    "windows-x86_64",
    "darwin-aarch64",
    "darwin-x86_64",
    "darwin-universal",
    "linux-x86_64",
];

const SCHEMA_CODE: &str = "E_SCHEMA";
const INSTALL_DUPLICATE_CODE: &str = "E_INSTALL_DUPLICATE";

/// `contract.js` `INSTALL_PATTERNS` key order (`:138-147`): the order `check_install_values` walks.
const INSTALL_FIELDS: [&str; 5] = [
    "vst3_bundle",
    "au_component",
    "preset_subdir",
    "windows_uninstall_key",
    "macos_pkg_id",
];
/// The fields that also refuse a reserved device stem (`contract.js:148`).
const NAMED_INSTALL_FIELDS: [&str; 3] = ["vst3_bundle", "au_component", "preset_subdir"];
/// `contract.js:150` `UNIQUE_INSTALL_FIELDS`, each with its `field` label (`install.<field>`).
const UNIQUE_INSTALL_FIELDS: [(&str, &str); 4] = [
    ("vst3_bundle", "install.vst3_bundle"),
    ("au_component", "install.au_component"),
    ("windows_uninstall_key", "install.windows_uninstall_key"),
    ("macos_pkg_id", "install.macos_pkg_id"),
];
/// The one unique field compared exactly; the others are ASCII case folded (`contract.js:151`).
const EXACT_UNIQUE_FIELD: &str = "windows_uninstall_key";

const UNTRUSTED_PREFIX: &str = "untrusted comment: ";
const TRUSTED_PREFIX: &str = "trusted comment: ";
/// `ED` + 8-byte key id + 64-byte signature.
const SIG_BLOB_BYTES: usize = 74;
const GLOBAL_SIG_BYTES: usize = 64;

// The `contract.js:120-147` patterns, written out (ECMA syntax; compiled through
// `ecma_pattern_to_rust` like the schema's own).
const SEMVER_PATTERN: &str = r"^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})(-(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*)?$";
const PLUGIN_NAME_PATTERN: &str = "^[A-Z][A-Za-z0-9]{0,63}$";
const RESERVED_STEM_PATTERN: &str = r"^([Cc][Oo][Nn]|[Pp][Rr][Nn]|[Aa][Uu][Xx]|[Nn][Uu][Ll]|[Cc][Oo][Mm][0-9]|[Ll][Pp][Tt][0-9])(\..*)?$";
const ASSET_NAME_PATTERN: &str = "^[A-Za-z0-9][A-Za-z0-9._-]*$";

/// The ECMA-262 `WhiteSpace` + `LineTerminator` set `\s` stands for, as class ranges (FR-002).
const ECMA_WHITESPACE: &str = r"\x{9}-\x{D}\x{20}\x{A0}\x{1680}\x{2000}-\x{200A}\x{2028}\x{2029}\x{202F}\x{205F}\x{3000}\x{FEFF}";
/// ECMA-262 `.` without the `s` flag.
const ECMA_DOT: &str = r"[^\n\r\x{2028}\x{2029}]";

/// Empty means valid (FR-007). Panics if an embedded schema does not compile: they are build
/// inputs, and `embedded_schemas_compile` proves both compile.
pub fn validate(doc: &Value, schema: Schema) -> Vec<Finding> {
    static V2: OnceLock<Result<Compiled, ContractError>> = OnceLock::new();
    static V1: OnceLock<Result<Compiled, ContractError>> = OnceLock::new();
    let (cell, text, name) = match schema {
        Schema::V2 => (&V2, V2_SCHEMA_TEXT, "v2"),
        Schema::V1 => (&V1, V1_SCHEMA_TEXT, "v1"),
    };
    let compiled = match cell.get_or_init(|| compile_schema_text(text)) {
        Ok(compiled) => compiled,
        Err(e) => panic!("the embedded {name} schema does not compile: {e}"),
    };
    let mut out = compiled.evaluate(doc);
    if schema == Schema::V2 {
        feed_code_findings(doc, &mut out);
    }
    out
}

/// Four lines, LF or CRLF, optional final line end; no cryptography (FR-005, `contract.js:363-384`).
pub fn parse_minisig_text(text: &str) -> Result<MinisigText, ContractError> {
    // `text.split(/\r?\n/u)`: one CR is part of the separator only before an LF.
    let mut lines: Vec<&str> = text.split('\n').collect();
    let ended = lines.len() - 1;
    for line in &mut lines[..ended] {
        *line = line.strip_suffix('\r').unwrap_or(line);
    }
    if lines.last() == Some(&"") {
        lines.pop();
    }
    let &[line1, line2, line3, line4] = lines.as_slice() else {
        return Err(sig_error(&format!("{} lines, not 4", lines.len())));
    };
    if lines.iter().any(|l| l.is_empty() || l.contains('\r')) {
        return Err(sig_error("an empty line or a stray CR"));
    }
    let untrusted = line1
        .strip_prefix(UNTRUSTED_PREFIX)
        .ok_or_else(|| sig_error("line 1 does not start \"untrusted comment: \""))?;
    let trusted = line3
        .strip_prefix(TRUSTED_PREFIX)
        .ok_or_else(|| sig_error("line 3 does not start \"trusted comment: \""))?;
    let blob = decode_canonical_base64(line2, "line 2")?;
    if blob.len() != SIG_BLOB_BYTES {
        return Err(sig_error(&format!(
            "line 2 decodes to {} bytes, not {SIG_BLOB_BYTES}",
            blob.len()
        )));
    }
    if blob[0] != 0x45 || blob[1] != 0x44 {
        return Err(sig_error("line 2 algorithm is not 'ED'"));
    }
    let global = decode_canonical_base64(line4, "line 4")?;
    if global.len() != GLOBAL_SIG_BYTES {
        return Err(sig_error(&format!(
            "line 4 decodes to {} bytes, not {GLOBAL_SIG_BYTES}",
            global.len()
        )));
    }
    let mut key_id = [0u8; 8];
    key_id.copy_from_slice(&blob[2..10]);
    let mut signature = [0u8; 64];
    signature.copy_from_slice(&blob[10..SIG_BLOB_BYTES]);
    let mut global_signature = [0u8; 64];
    global_signature.copy_from_slice(&global);
    Ok(MinisigText {
        untrusted_comment: untrusted.to_owned(),
        key_id,
        signature,
        trusted_comment: trusted.to_owned(),
        global_signature,
    })
}

/// `krate:1:<Name>:<version>:<platform-key>:<asset>` (FR-006), checked in `contract.js:299-318`
/// order.
pub fn parse_trusted_comment(s: &str) -> Result<TrustedComment, ContractError> {
    let fields_error = |why: String| rule(RuleCode::TcFields, format!("trusted comment: {why}"));
    if s.contains(['\r', '\n']) {
        return Err(fields_error(format!("holds a CR or LF: {}", show(s))));
    }
    let f: Vec<&str> = s.split(':').collect();
    let &[prefix, schema, name, version, platform, asset] = f.as_slice() else {
        return Err(fields_error(format!(
            "{} ':' fields, not 6: {}",
            f.len(),
            show(s)
        )));
    };
    if prefix != "krate" || schema != "1" {
        return Err(fields_error(format!("prefix is not krate:1: {}", show(s))));
    }
    let invalid = |code, field: &str, value: &str| {
        Err(rule(
            code,
            format!("trusted comment: invalid {field} {}", show(value)),
        ))
    };
    if !(plugin_name_re().is_match(name) && !reserved_stem_re().is_match(name)) {
        return invalid(RuleCode::TcName, "name", name);
    }
    if !semver_re().is_match(version) {
        return invalid(RuleCode::TcVersion, "version", version);
    }
    if !PLATFORM_KEYS.contains(&platform) {
        return invalid(RuleCode::TcPlatform, "platform", platform);
    }
    if !(asset_name_re().is_match(asset) && asset != "." && asset != "..") {
        return invalid(RuleCode::TcAsset, "asset", asset);
    }
    Ok(TrustedComment {
        name: name.to_owned(),
        version: version.to_owned(),
        platform: platform.to_owned(),
        asset: asset.to_owned(),
    })
}

/// One field, FR-004 order (`contract.js` `checkInstallValues`, `:226-242`). A field outside the
/// five install fields is refused, never passed unchecked.
pub fn check_install_value(field: &str, value: &Value) -> Result<(), ContractError> {
    let fail = |why: &str| {
        Err(rule(
            RuleCode::InstallValue,
            format!("install.{field}: {why}: {value}"),
        ))
    };
    let Some(pattern) = install_pattern(field) else {
        return fail("not an install field");
    };
    let Some(s) = value.as_str() else {
        return fail("not a string");
    };
    if s.contains(['/', '\\', ':']) {
        return fail("contains a path separator or a colon");
    }
    if s.contains("..") {
        return fail("contains '..'");
    }
    if field == "preset_subdir" && s.ends_with(' ') {
        return fail("ends in a space");
    }
    if NAMED_INSTALL_FIELDS.contains(&field) && reserved_stem_re().is_match(s) {
        return fail("a Windows reserved device name");
    }
    if !pattern.is_match(s) {
        return fail("outside its FR-008 pattern");
    }
    Ok(())
}

/// RFC 6901 pointer from path segments (`contract.js:402-403`): `~` → `~0`, `/` → `~1`.
pub(crate) fn pointer(segments: &[&str]) -> String {
    segments
        .iter()
        .map(|s| format!("/{}", s.replace('~', "~0").replace('/', "~1")))
        .collect()
}

/// Rewrites an ECMA-262 (`u` flag) pattern into `regex` syntax with the same meaning (FR-002,
/// PD-11): `\s` becomes the ECMA set and `.` outside a class `[^\n\r\x{2028}\x{2029}]`; the
/// escapes whose meanings differ (`\S \d \D \w \W \b \B`) and an empty class (`[]`, `[^]`,
/// which `regex` reads differently) are `Unsupported`. Inside a class `[`, `&` and `~` are
/// escaped (in `regex` they open a nested class or a set operation; in ECMA they are literal).
/// Everything else is copied; `Regex::new` refuses what neither dialect shares.
pub(crate) fn ecma_pattern_to_rust(pattern: &str) -> Result<String, ContractError> {
    let refuse = |what: &str| unsupported("", what);
    let mut out = String::with_capacity(pattern.len());
    let mut in_class = false;
    let mut chars = pattern.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '\\' => {
                let Some(e) = chars.next() else {
                    return Err(refuse("a trailing \\"));
                };
                match e {
                    's' if in_class => out.push_str(ECMA_WHITESPACE),
                    's' => {
                        out.push('[');
                        out.push_str(ECMA_WHITESPACE);
                        out.push(']');
                    }
                    'S' | 'd' | 'D' | 'w' | 'W' | 'b' | 'B' => {
                        return Err(refuse(&format!("\\{e}")));
                    }
                    _ => {
                        out.push('\\');
                        out.push(e);
                    }
                }
            }
            '[' if !in_class => {
                in_class = true;
                out.push('[');
                if chars.peek() == Some(&'^') {
                    chars.next();
                    out.push('^');
                }
                if chars.peek() == Some(&']') {
                    return Err(refuse("an empty class"));
                }
            }
            ']' if in_class => {
                in_class = false;
                out.push(']');
            }
            '[' | '&' | '~' if in_class => {
                out.push('\\');
                out.push(c);
            }
            '.' if !in_class => out.push_str(ECMA_DOT),
            _ => out.push(c),
        }
    }
    Ok(out)
}

// --- the schema interpreter (plan §4.2.2)

type NodeId = usize;

/// A compiled schema: an arena of nodes, `$defs` first (PD-24).
pub(crate) struct Compiled {
    root: NodeId,
    arena: Vec<Node>,
}

enum Node {
    Bool(bool),
    Schema(Box<Keywords>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum JsonType {
    Object,
    String,
    Integer,
}

impl JsonType {
    fn name(self) -> &'static str {
        match self {
            JsonType::Object => "object",
            JsonType::String => "string",
            JsonType::Integer => "integer",
        }
    }

    fn matches(self, v: &Value) -> bool {
        match (self, v) {
            (JsonType::Object, Value::Object(_)) | (JsonType::String, Value::String(_)) => true,
            (JsonType::Integer, Value::Number(n)) => {
                n.is_u64() || n.is_i64() || n.as_f64().is_some_and(|f| f.fract() == 0.0)
            }
            _ => false,
        }
    }
}

#[derive(Default)]
struct Keywords {
    types: Option<Vec<JsonType>>,
    properties: Vec<(String, NodeId)>,
    pattern_properties: Vec<(Regex, NodeId)>,
    additional: Option<NodeId>,
    property_names: Option<NodeId>,
    required: Vec<String>,
    dependent_required: Vec<(String, Vec<String>)>,
    constant: Option<Value>,
    enumeration: Option<Vec<Value>>,
    pattern: Option<Regex>,
    minimum: Option<f64>,
    maximum: Option<f64>,
    not: Option<NodeId>,
    all_of: Vec<NodeId>,
    if_then_else: Option<(NodeId, Option<NodeId>, Option<NodeId>)>,
    reference: Option<NodeId>,
}

/// Compiles schema text into the interpreter (FR-001, PD-24). `Unsupported { pointer, keyword }`
/// names the schema object holding the refused keyword; text that is not JSON is
/// `("", "JSON")`, a schema that is neither an object nor a boolean `(<holder>, <keyword>)`
/// (`("", "schema")` at the root).
pub(crate) fn compile_schema_text(text: &str) -> Result<Compiled, ContractError> {
    let root: Value = serde_json::from_str(text).map_err(|_| unsupported("", "JSON"))?;
    let mut c = Compiler {
        defs: BTreeMap::new(),
        arena: Vec::new(),
    };
    let defs = match root.get("$defs") {
        None => None,
        Some(Value::Object(defs)) => Some(defs),
        Some(_) => return Err(unsupported("", "$defs")),
    };
    // Pass 1: one slot per definition, so a `$ref` resolves whatever the definition order.
    for name in defs.into_iter().flat_map(Map::keys) {
        c.defs.insert(name.clone(), c.arena.len());
        c.arena.push(Node::Bool(true));
    }
    // Pass 2: the definitions, then the root.
    for (name, def) in defs.into_iter().flatten() {
        let id = c.defs[name];
        c.arena[id] = c.build(def, &pointer(&["$defs", name]), ("", "$defs"))?;
    }
    let root = c.child(&root, "", ("", "schema"))?;
    Ok(Compiled {
        root,
        arena: c.arena,
    })
}

struct Compiler {
    defs: BTreeMap<String, NodeId>,
    arena: Vec<Node>,
}

impl Compiler {
    fn build(
        &mut self,
        schema: &Value,
        at: &str,
        holder: (&str, &str),
    ) -> Result<Node, ContractError> {
        match schema {
            Value::Bool(b) => Ok(Node::Bool(*b)),
            Value::Object(map) => Ok(Node::Schema(Box::new(self.keywords(map, at)?))),
            _ => Err(unsupported(holder.0, holder.1)),
        }
    }

    fn child(
        &mut self,
        schema: &Value,
        at: &str,
        holder: (&str, &str),
    ) -> Result<NodeId, ContractError> {
        let node = self.build(schema, at, holder)?;
        self.arena.push(node);
        Ok(self.arena.len() - 1)
    }

    fn keywords(&mut self, map: &Map<String, Value>, at: &str) -> Result<Keywords, ContractError> {
        let mut k = Keywords::default();
        let (mut if_, mut then_, mut else_) = (None, None, None);
        for (key, value) in map {
            let kw = key.as_str();
            let refuse = || unsupported(at, kw);
            let sub = |segments: &[&str]| format!("{at}{}", pointer(segments));
            match kw {
                "$schema" | "$id" | "title" | "$comment" | "$defs" => {}
                "type" => k.types = Some(json_types(value).ok_or_else(refuse)?),
                "properties" => {
                    let Value::Object(props) = value else {
                        return Err(refuse());
                    };
                    for (name, schema) in props {
                        let id = self.child(schema, &sub(&[kw, name]), (at, kw))?;
                        k.properties.push((name.clone(), id));
                    }
                }
                "patternProperties" => {
                    let Value::Object(props) = value else {
                        return Err(refuse());
                    };
                    for (pattern, schema) in props {
                        let re = compile_pattern(pattern).map_err(|_| refuse())?;
                        let id = self.child(schema, &sub(&[kw, pattern]), (at, kw))?;
                        k.pattern_properties.push((re, id));
                    }
                }
                "additionalProperties" => {
                    k.additional = Some(self.child(value, &sub(&[kw]), (at, kw))?);
                }
                "propertyNames" => {
                    k.property_names = Some(self.child(value, &sub(&[kw]), (at, kw))?);
                }
                "required" => k.required = string_list(value).ok_or_else(refuse)?,
                "dependentRequired" => {
                    let Value::Object(deps) = value else {
                        return Err(refuse());
                    };
                    for (name, list) in deps {
                        let list = string_list(list).ok_or_else(refuse)?;
                        k.dependent_required.push((name.clone(), list));
                    }
                }
                "const" => k.constant = Some(value.clone()),
                "enum" => {
                    let Value::Array(values) = value else {
                        return Err(refuse());
                    };
                    k.enumeration = Some(values.clone());
                }
                "pattern" => {
                    let Value::String(pattern) = value else {
                        return Err(refuse());
                    };
                    k.pattern = Some(compile_pattern(pattern).map_err(|_| refuse())?);
                }
                "minimum" => k.minimum = Some(value.as_f64().ok_or_else(refuse)?),
                "maximum" => k.maximum = Some(value.as_f64().ok_or_else(refuse)?),
                "not" => k.not = Some(self.child(value, &sub(&[kw]), (at, kw))?),
                "allOf" => {
                    let Value::Array(schemas) = value else {
                        return Err(refuse());
                    };
                    if schemas.is_empty() {
                        return Err(refuse());
                    }
                    for (i, schema) in schemas.iter().enumerate() {
                        let id = self.child(schema, &sub(&[kw, &i.to_string()]), (at, kw))?;
                        k.all_of.push(id);
                    }
                }
                "if" => if_ = Some(self.child(value, &sub(&[kw]), (at, kw))?),
                "then" => then_ = Some(self.child(value, &sub(&[kw]), (at, kw))?),
                "else" => else_ = Some(self.child(value, &sub(&[kw]), (at, kw))?),
                "$ref" => k.reference = Some(self.reference(value).ok_or_else(refuse)?),
                _ => return Err(refuse()),
            }
        }
        // 2020-12: `then` / `else` without `if` have no effect (compiled above all the same, so
        // an unsupported keyword inside them is still refused).
        if let Some(i) = if_ {
            k.if_then_else = Some((i, then_, else_));
        }
        Ok(k)
    }

    /// `#/$defs/<name>` naming a definition, nothing else.
    fn reference(&self, value: &Value) -> Option<NodeId> {
        let token = value.as_str()?.strip_prefix("#/$defs/")?;
        if token.contains('/') {
            return None;
        }
        let name = token.replace("~1", "/").replace("~0", "~");
        self.defs.get(&name).copied()
    }
}

fn json_types(value: &Value) -> Option<Vec<JsonType>> {
    let one = |v: &Value| match v.as_str()? {
        "object" => Some(JsonType::Object),
        "string" => Some(JsonType::String),
        "integer" => Some(JsonType::Integer),
        _ => None,
    };
    match value {
        Value::Array(list) if !list.is_empty() => list.iter().map(one).collect(),
        Value::Array(_) => None,
        v => Some(vec![one(v)?]),
    }
}

fn string_list(value: &Value) -> Option<Vec<String>> {
    value
        .as_array()?
        .iter()
        .map(|v| v.as_str().map(str::to_owned))
        .collect()
}

fn compile_pattern(pattern: &str) -> Result<Regex, ContractError> {
    let rust = ecma_pattern_to_rust(pattern)?;
    Regex::new(&rust).map_err(|e| unsupported("", &format!("pattern: {e}")))
}

impl Compiled {
    /// Every schema finding of `doc` (Ajv `allErrors`, PD-10).
    pub(crate) fn evaluate(&self, doc: &Value) -> Vec<Finding> {
        let mut out = Vec::new();
        self.eval(self.root, doc, "", &mut out);
        out
    }

    /// Valid? Every failure adds at least one finding, so valid means none were added.
    fn eval(&self, id: NodeId, inst: &Value, path: &str, out: &mut Vec<Finding>) -> bool {
        let start = out.len();
        match &self.arena[id] {
            Node::Bool(true) => {}
            Node::Bool(false) => out.push(schema_finding(
                path,
                "false schema",
                "boolean schema is false",
            )),
            Node::Schema(k) => self.eval_keywords(k, inst, path, out),
        }
        out.len() == start
    }

    fn eval_keywords(&self, k: &Keywords, inst: &Value, path: &str, out: &mut Vec<Finding>) {
        if let Some(r) = k.reference {
            self.eval(r, inst, path, out);
        }
        if let Some(types) = &k.types
            && !types.iter().any(|t| t.matches(inst))
        {
            let names: Vec<&str> = types.iter().map(|t| t.name()).collect();
            out.push(schema_finding(
                path,
                "type",
                &format!("must be {}", names.join(",")),
            ));
        }
        if let Some(c) = &k.constant
            && !json_equal(c, inst)
        {
            out.push(schema_finding(path, "const", "must be equal to constant"));
        }
        if let Some(values) = &k.enumeration
            && !values.iter().any(|v| json_equal(v, inst))
        {
            out.push(schema_finding(
                path,
                "enum",
                "must be equal to one of the allowed values",
            ));
        }
        if let (Some(re), Value::String(s)) = (&k.pattern, inst)
            && !re.is_match(s)
        {
            out.push(schema_finding(
                path,
                "pattern",
                &format!("must match pattern \"{}\"", re.as_str()),
            ));
        }
        if let Some(n) = inst.as_f64() {
            if let Some(min) = k.minimum
                && n < min
            {
                out.push(schema_finding(
                    path,
                    "minimum",
                    &format!("must be >= {min}"),
                ));
            }
            if let Some(max) = k.maximum
                && n > max
            {
                out.push(schema_finding(
                    path,
                    "maximum",
                    &format!("must be <= {max}"),
                ));
            }
        }
        if let Value::Object(obj) = inst {
            self.eval_object(k, obj, path, out);
        }
        if let Some(n) = k.not {
            let mut discarded = Vec::new();
            if self.eval(n, inst, path, &mut discarded) {
                out.push(schema_finding(path, "not", "must NOT be valid"));
            }
        }
        for &a in &k.all_of {
            self.eval(a, inst, path, out);
        }
        if let Some((cond, then_, else_)) = k.if_then_else {
            let mut discarded = Vec::new();
            let (branch, name) = if self.eval(cond, inst, path, &mut discarded) {
                (then_, "then")
            } else {
                (else_, "else")
            };
            if let Some(b) = branch
                && !self.eval(b, inst, path, out)
            {
                out.push(schema_finding(
                    path,
                    "if",
                    &format!("must match \"{name}\" schema"),
                ));
            }
        }
    }

    fn eval_object(
        &self,
        k: &Keywords,
        obj: &Map<String, Value>,
        path: &str,
        out: &mut Vec<Finding>,
    ) {
        for name in &k.required {
            if !obj.contains_key(name) {
                out.push(schema_finding(
                    path,
                    "required",
                    &format!("must have required property '{name}'"),
                ));
            }
        }
        for (name, deps) in &k.dependent_required {
            if !obj.contains_key(name) {
                continue;
            }
            for dep in deps.iter().filter(|d| !obj.contains_key(*d)) {
                out.push(schema_finding(
                    path,
                    "dependentRequired",
                    &format!("must have property {dep} when property {name} is present"),
                ));
            }
        }
        for (name, child) in &k.properties {
            if let Some(v) = obj.get(name) {
                self.eval(*child, v, &format!("{path}{}", pointer(&[name])), out);
            }
        }
        for (key, v) in obj {
            let child_path = format!("{path}{}", pointer(&[key]));
            let mut matched = k.properties.iter().any(|(name, _)| name == key);
            for (re, child) in &k.pattern_properties {
                if re.is_match(key) {
                    matched = true;
                    self.eval(*child, v, &child_path, out);
                }
            }
            match k.additional {
                Some(a) if !matched => {
                    if matches!(self.arena[a], Node::Bool(false)) {
                        // Ajv reports `additionalProperties: false` itself, at the object.
                        out.push(schema_finding(
                            path,
                            "additionalProperties",
                            "must NOT have additional properties",
                        ));
                    } else {
                        self.eval(a, v, &child_path, out);
                    }
                }
                _ => {}
            }
        }
        if let Some(names) = k.property_names {
            for key in obj.keys() {
                // The key is a string instance located at the object's path (FR-002).
                if !self.eval(names, &Value::String(key.clone()), path, out) {
                    out.push(schema_finding(
                        path,
                        "propertyNames",
                        "property name must be valid",
                    ));
                }
            }
        }
    }
}

/// JSON equality, numbers by value (FR-002).
fn json_equal(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Number(x), Value::Number(y)) => {
            if let (Some(p), Some(q)) = (x.as_i64(), y.as_i64()) {
                p == q
            } else if let (Some(p), Some(q)) = (x.as_u64(), y.as_u64()) {
                p == q
            } else {
                x.as_f64() == y.as_f64()
            }
        }
        (Value::Array(x), Value::Array(y)) => {
            x.len() == y.len() && x.iter().zip(y).all(|(p, q)| json_equal(p, q))
        }
        (Value::Object(x), Value::Object(y)) => {
            x.len() == y.len()
                && x.iter()
                    .all(|(key, p)| y.get(key).is_some_and(|q| json_equal(p, q)))
        }
        _ => a == b,
    }
}

fn schema_finding(path: &str, keyword: &'static str, what: &str) -> Finding {
    let at = if path.is_empty() { "/" } else { path };
    Finding {
        code: SCHEMA_CODE,
        instance_path: path.to_owned(),
        keyword: Some(keyword),
        message: format!("{at} {what}"),
        duplicate: None,
    }
}

// --- the code rules (FR-004, plan §4.2.3; a port of `contract.js:421-478`)

fn feed_code_findings(doc: &Value, out: &mut Vec<Finding>) {
    let Some(plugins) = doc.get("plugins").and_then(Value::as_object) else {
        return;
    };
    for (plugin, entry) in plugins {
        if !entry.is_object() {
            continue;
        }
        let mut parts = vec![(entry, pointer(&["plugins", plugin]))];
        if let Some(sibling) = entry.get("prerelease").filter(|v| v.is_object()) {
            parts.push((sibling, pointer(&["plugins", plugin, "prerelease"])));
        }
        for (part, base) in parts {
            signature_findings(part, &base, out);
            if let Some(install) = part.get("install").and_then(Value::as_object) {
                // The first failing field only (`contract.js:226-242` throws).
                let failed = INSTALL_FIELDS.iter().find_map(|field| {
                    let value = install.get(*field)?;
                    check_install_value(field, value).err().map(|e| (field, e))
                });
                if let Some((field, e)) = failed {
                    out.push(rule_finding(
                        e,
                        format!("{base}{}", pointer(&["install", field])),
                    ));
                }
            }
        }
    }
    duplicate_findings(plugins, out);
}

fn signature_findings(part: &Value, base: &str, out: &mut Vec<Finding>) {
    let Some(platforms) = part.get("platforms").and_then(Value::as_object) else {
        return;
    };
    for (key, platform) in platforms {
        // A non-object platform or a non-string signature is the schema's to report.
        let Some(signature) = platform.get("signature").and_then(Value::as_str) else {
            continue;
        };
        let checked = if signature.contains('\r') {
            Err(sig_error("holds a CR (LF line ends only, D7)"))
        } else {
            parse_minisig_text(signature).and_then(|parsed| {
                // C3 names only the defined keys; a reader ignores any other key (D1).
                if PLATFORM_KEYS.contains(&key.as_str()) {
                    parse_trusted_comment(&parsed.trusted_comment).map(drop)
                } else {
                    Ok(())
                }
            })
        };
        if let Err(e) = checked {
            out.push(rule_finding(
                e,
                format!("{base}{}", pointer(&["platforms", key, "signature"])),
            ));
        }
    }
}

/// `contract.js` `findInstallDuplicates` (`:249-271`) over the plugin keys in bytewise order
/// (SI-8): one finding at the second plugin's field, both sides in `DuplicateSides` (PD-9).
fn duplicate_findings(plugins: &Map<String, Value>, out: &mut Vec<Finding>) {
    let mut names: Vec<&String> = plugins.keys().collect();
    names.sort();
    // (field, folded value) -> (first plugin, its spelling, its full path)
    let mut seen: BTreeMap<(&str, String), (&str, &str, String)> = BTreeMap::new();
    for plugin in names {
        let entry = &plugins[plugin];
        if !entry.is_object() {
            continue;
        }
        let sibling = entry.get("prerelease").filter(|v| v.is_object());
        let installs = [
            (
                entry.get("install"),
                pointer(&["plugins", plugin, "install"]),
            ),
            (
                sibling.and_then(|s| s.get("install")),
                pointer(&["plugins", plugin, "prerelease", "install"]),
            ),
        ];
        for (field, label) in UNIQUE_INSTALL_FIELDS {
            // An entry and its own sibling are one plugin.
            let mut mine = BTreeSet::new();
            for (install, base) in &installs {
                let Some(value) = install.and_then(|i| i.get(field)).and_then(Value::as_str) else {
                    continue;
                };
                let folded = if field == EXACT_UNIQUE_FIELD {
                    value.to_owned()
                } else {
                    value.to_ascii_lowercase()
                };
                if !mine.insert(folded.clone()) {
                    continue;
                }
                let path = format!("{base}{}", pointer(&[field]));
                if let Some((first, first_value, first_path)) = seen.get(&(field, folded.clone())) {
                    out.push(Finding {
                        code: INSTALL_DUPLICATE_CODE,
                        instance_path: path.clone(),
                        keyword: None,
                        message: format!(
                            "{label} {} is shared by plugins {} and {} (FR-014)",
                            show(first_value),
                            show(first),
                            show(plugin)
                        ),
                        duplicate: Some(DuplicateSides {
                            field: label,
                            first: first_path.clone(),
                            second: path,
                        }),
                    });
                } else {
                    seen.insert((field, folded), (plugin.as_str(), value, path));
                }
            }
        }
    }
}

fn rule_finding(err: ContractError, instance_path: String) -> Finding {
    let code = match &err {
        ContractError::Rule { code, .. } => code.as_str(),
        ContractError::Unsupported { .. } => "E_SCHEMA_UNSUPPORTED",
    };
    Finding {
        code,
        message: format!("{instance_path}: {err}"),
        instance_path,
        keyword: None,
        duplicate: None,
    }
}

// --- helpers

fn rule(code: RuleCode, message: String) -> ContractError {
    ContractError::Rule { code, message }
}

fn sig_error(why: &str) -> ContractError {
    rule(RuleCode::SignatureText, format!("signature text: {why}"))
}

fn unsupported(pointer: &str, keyword: &str) -> ContractError {
    ContractError::Unsupported {
        pointer: pointer.to_owned(),
        keyword: keyword.to_owned(),
    }
}

/// `contract.js` `show`: the JSON text of a string.
fn show(s: &str) -> String {
    Value::from(s).to_string()
}

/// Standard base64 whose re-encode equals the input (`contract.js` `decodeCanonicalBase64`).
fn decode_canonical_base64(s: &str, what: &str) -> Result<Vec<u8>, ContractError> {
    let not_canonical = || sig_error(&format!("{what} is not canonical base64"));
    let bytes = STANDARD.decode(s).map_err(|_| not_canonical())?;
    if STANDARD.encode(&bytes) != s {
        return Err(not_canonical());
    }
    Ok(bytes)
}

fn cached(cell: &'static OnceLock<Regex>, pattern: &str) -> &'static Regex {
    cell.get_or_init(|| {
        compile_pattern(pattern).unwrap_or_else(|e| panic!("contract pattern {pattern}: {e}"))
    })
}

fn semver_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached(&RE, SEMVER_PATTERN)
}

fn plugin_name_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached(&RE, PLUGIN_NAME_PATTERN)
}

pub(crate) fn reserved_stem_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached(&RE, RESERVED_STEM_PATTERN)
}

fn asset_name_re() -> &'static Regex {
    static RE: OnceLock<Regex> = OnceLock::new();
    cached(&RE, ASSET_NAME_PATTERN)
}

/// `contract.js` `INSTALL_PATTERNS` (FR-008 as amended by PD-14).
fn install_pattern(field: &str) -> Option<&'static Regex> {
    Some(match field {
        "vst3_bundle" => {
            static RE: OnceLock<Regex> = OnceLock::new();
            cached(&RE, r"^[A-Za-z0-9][A-Za-z0-9 _.-]*\.vst3$")
        }
        "au_component" => {
            static RE: OnceLock<Regex> = OnceLock::new();
            cached(&RE, r"^[A-Za-z0-9][A-Za-z0-9 _.-]*\.component$")
        }
        "preset_subdir" => {
            static RE: OnceLock<Regex> = OnceLock::new();
            cached(&RE, "^[A-Za-z0-9]([A-Za-z0-9 _-]*[A-Za-z0-9_-])?$")
        }
        "windows_uninstall_key" => {
            static RE: OnceLock<Regex> = OnceLock::new();
            cached(
                &RE,
                r"^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}_is1$",
            )
        }
        "macos_pkg_id" => {
            static RE: OnceLock<Regex> = OnceLock::new();
            cached(&RE, r"^com\.krateaudio\.[a-z0-9]+$")
        }
        _ => return None,
    })
}

#[cfg(test)]
#[path = "contract_tests.rs"]
mod tests;
