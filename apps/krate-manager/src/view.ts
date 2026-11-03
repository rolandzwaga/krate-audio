// The status view the core sends (spec FR-046, FR-051; plan §4.10, PD-12). The goldens under
// tests/fixtures/status/ reach the renderer only through `toStatusView`, which checks every key,
// type and enum value, so a fixture that drifts from these types fails `npm test`.

export type OsName = "windows" | "macos" | "linux";
export type FeedSource = "fresh" | "cached" | "none";
export type Availability = "available" | "not_through_manager" | "not_for_this_machine" | "needs_newer_os";
export type Installed = "not_checked" | "not_installed" | "elsewhere" | "version_unknown" | "version";
export type Update = "not_installed" | "up_to_date" | "update_available" | "installed_newer" | "unknown";
export type Format = "vst3" | "au";
export type Place = "configured" | "vst3_user" | "vst3_global" | "au_user" | "au_global" | "installer_location";
export type VersionSource = "classes" | "top_level" | "au_plist";
export type UnknownReason =
  | "single_file"
  | "no_moduleinfo"
  | "too_large"
  | "unreadable"
  | "corrupt"
  | "classes_disagree"
  | "no_version"
  | "not_a_bundle"
  | "au_no_version";
export type RecordKind = "inno" | "pkg" | "manifest";
export type ActionName = "install" | "update" | "remove";

export type Inline =
  | { readonly type: "text"; readonly text: string }
  | { readonly type: "strong"; readonly inlines: readonly Inline[] }
  | { readonly type: "emphasis"; readonly inlines: readonly Inline[] }
  | { readonly type: "code"; readonly text: string }
  | { readonly type: "break" };

export type Block =
  | { readonly type: "heading"; readonly level: number; readonly inlines: readonly Inline[] }
  | { readonly type: "paragraph"; readonly inlines: readonly Inline[] }
  | {
      readonly type: "list";
      readonly ordered: boolean;
      readonly start: number | null;
      readonly items: readonly (readonly Block[])[];
    }
  | { readonly type: "quote"; readonly blocks: readonly Block[] }
  | { readonly type: "code"; readonly text: string }
  | { readonly type: "rule" };

export interface FeedView {
  readonly source: FeedSource;
  readonly fetched_at_unix: number | null;
  readonly schema_version: number | null;
  readonly error: string | null;
}

export interface CopyView {
  readonly format: Format;
  readonly place: Place;
  readonly vendor_folder: boolean;
  readonly nested: boolean;
  readonly path: string;
  readonly managed: boolean;
  readonly loads_before_managed: boolean;
  readonly version: string | null;
  readonly version_source: VersionSource | null;
  readonly unknown_reason: UnknownReason | null;
}

export interface RecordView {
  readonly kind: RecordKind;
  readonly version: string | null;
  readonly files: number | null;
}

/** Where a card points the user to remove a copy the manager may not remove (Phase 10 FR-083, FR-134). */
export type RemoveHint = "windows_settings";

export interface PluginView {
  readonly name: string;
  readonly latest: string;
  readonly availability: Availability;
  readonly min_os: string | null;
  readonly installed: Installed;
  readonly installed_version: string | null;
  readonly update: Update | null;
  readonly copies: readonly CopyView[];
  readonly record: RecordView | null;
  readonly record_error: string | null;
  readonly record_without_files: boolean;
  readonly record_version_differs: boolean;
  readonly release_page_url: string | null;
  /** Phase 12 FR-088: the plugin's krateaudio.com page. */
  readonly plugin_page_url: string | null;
  readonly notes: readonly Block[];
  readonly actions: readonly ActionName[];
  readonly remove_hint: RemoveHint | null;
}

/** A plugin the manager installed that the feed no longer offers (Phase 12 FR-052, PD-13). */
export interface OrphanView {
  readonly name: string;
  readonly version: string;
}

export interface StatusView {
  readonly os: OsName;
  readonly edge_max_age_secs: number;
  readonly running_os: string | null;
  readonly feed: FeedView;
  readonly plugins: readonly PluginView[];
  readonly orphans: readonly OrphanView[];
}

const OS_NAMES: readonly OsName[] = ["windows", "macos", "linux"];
const FEED_SOURCES: readonly FeedSource[] = ["fresh", "cached", "none"];
const AVAILABILITIES: readonly Availability[] = ["available", "not_through_manager", "not_for_this_machine", "needs_newer_os"];
const INSTALLED: readonly Installed[] = ["not_checked", "not_installed", "elsewhere", "version_unknown", "version"];
const UPDATES: readonly Update[] = ["not_installed", "up_to_date", "update_available", "installed_newer", "unknown"];
const FORMATS: readonly Format[] = ["vst3", "au"];
const PLACES: readonly Place[] = ["configured", "vst3_user", "vst3_global", "au_user", "au_global", "installer_location"];
const VERSION_SOURCES: readonly VersionSource[] = ["classes", "top_level", "au_plist"];
const UNKNOWN_REASONS: readonly UnknownReason[] = [
  "single_file",
  "no_moduleinfo",
  "too_large",
  "unreadable",
  "corrupt",
  "classes_disagree",
  "no_version",
  "not_a_bundle",
  "au_no_version",
];
const RECORD_KINDS: readonly RecordKind[] = ["inno", "pkg", "manifest"];
const ACTION_NAMES: readonly ActionName[] = ["install", "update", "remove"];
const REMOVE_HINTS: readonly RemoveHint[] = ["windows_settings"];

function fail(path: string, expected: string): never {
  throw new TypeError(`${path}: ${expected}`);
}

/** The object at `path`, whose key set must be exactly `keys`. */
function object(value: unknown, path: string, keys: readonly string[]): Readonly<Record<string, unknown>> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) fail(path, "an object");
  const record = value as Readonly<Record<string, unknown>>;
  for (const key of keys) if (!(key in record)) fail(`${path}.${key}`, "present");
  for (const key of Object.keys(record)) if (!keys.includes(key)) fail(`${path}.${key}`, "no such key");
  return record;
}

function string(value: unknown, path: string): string {
  return typeof value === "string" ? value : fail(path, "a string");
}

function nullableString(value: unknown, path: string): string | null {
  return value === null ? null : string(value, path);
}

function boolean(value: unknown, path: string): boolean {
  return typeof value === "boolean" ? value : fail(path, "a boolean");
}

function count(value: unknown, path: string): number {
  return typeof value === "number" && Number.isInteger(value) && value >= 0 ? value : fail(path, "a non-negative integer");
}

function nullableCount(value: unknown, path: string): number | null {
  return value === null ? null : count(value, path);
}

function oneOf<T extends string>(value: unknown, path: string, allowed: readonly T[]): T {
  const found = allowed.find((candidate) => candidate === value);
  return found ?? fail(path, `one of ${allowed.join(", ")}`);
}

function nullableOneOf<T extends string>(value: unknown, path: string, allowed: readonly T[]): T | null {
  return value === null ? null : oneOf(value, path, allowed);
}

function array<T>(value: unknown, path: string, element: (item: unknown, at: string) => T): readonly T[] {
  if (!Array.isArray(value)) fail(path, "an array");
  return (value as readonly unknown[]).map((item, index) => element(item, `${path}[${String(index)}]`));
}

function typeOf(value: unknown, path: string): string {
  if (typeof value !== "object" || value === null || Array.isArray(value)) fail(path, "an object");
  return string((value as Readonly<Record<string, unknown>>)["type"], `${path}.type`);
}

function toInline(value: unknown, path: string): Inline {
  const type = typeOf(value, path);
  switch (type) {
    case "text":
    case "code": {
      const o = object(value, path, ["type", "text"]);
      return { type, text: string(o["text"], `${path}.text`) };
    }
    case "strong":
    case "emphasis": {
      const o = object(value, path, ["type", "inlines"]);
      return { type, inlines: array(o["inlines"], `${path}.inlines`, toInline) };
    }
    case "break":
      object(value, path, ["type"]);
      return { type };
    default:
      return fail(`${path}.type`, "one of text, strong, emphasis, code, break");
  }
}

function toBlock(value: unknown, path: string): Block {
  const type = typeOf(value, path);
  switch (type) {
    case "heading": {
      const o = object(value, path, ["type", "level", "inlines"]);
      return { type, level: count(o["level"], `${path}.level`), inlines: array(o["inlines"], `${path}.inlines`, toInline) };
    }
    case "paragraph": {
      const o = object(value, path, ["type", "inlines"]);
      return { type, inlines: array(o["inlines"], `${path}.inlines`, toInline) };
    }
    case "list": {
      const o = object(value, path, ["type", "ordered", "start", "items"]);
      return {
        type,
        ordered: boolean(o["ordered"], `${path}.ordered`),
        start: nullableCount(o["start"], `${path}.start`),
        items: array(o["items"], `${path}.items`, (item, at) => array(item, at, toBlock)),
      };
    }
    case "quote": {
      const o = object(value, path, ["type", "blocks"]);
      return { type, blocks: array(o["blocks"], `${path}.blocks`, toBlock) };
    }
    case "code": {
      const o = object(value, path, ["type", "text"]);
      return { type, text: string(o["text"], `${path}.text`) };
    }
    case "rule":
      object(value, path, ["type"]);
      return { type };
    default:
      return fail(`${path}.type`, "one of heading, paragraph, list, quote, code, rule");
  }
}

function toCopy(value: unknown, path: string): CopyView {
  const o = object(value, path, [
    "format",
    "place",
    "vendor_folder",
    "nested",
    "path",
    "managed",
    "loads_before_managed",
    "version",
    "version_source",
    "unknown_reason",
  ]);
  return {
    format: oneOf(o["format"], `${path}.format`, FORMATS),
    place: oneOf(o["place"], `${path}.place`, PLACES),
    vendor_folder: boolean(o["vendor_folder"], `${path}.vendor_folder`),
    nested: boolean(o["nested"], `${path}.nested`),
    path: string(o["path"], `${path}.path`),
    managed: boolean(o["managed"], `${path}.managed`),
    loads_before_managed: boolean(o["loads_before_managed"], `${path}.loads_before_managed`),
    version: nullableString(o["version"], `${path}.version`),
    version_source: nullableOneOf(o["version_source"], `${path}.version_source`, VERSION_SOURCES),
    unknown_reason: nullableOneOf(o["unknown_reason"], `${path}.unknown_reason`, UNKNOWN_REASONS),
  };
}

function toRecord(value: unknown, path: string): RecordView | null {
  if (value === null) return null;
  const o = object(value, path, ["kind", "version", "files"]);
  return {
    kind: oneOf(o["kind"], `${path}.kind`, RECORD_KINDS),
    version: nullableString(o["version"], `${path}.version`),
    files: nullableCount(o["files"], `${path}.files`),
  };
}

function toPlugin(value: unknown, path: string): PluginView {
  const o = object(value, path, [
    "name",
    "latest",
    "availability",
    "min_os",
    "installed",
    "installed_version",
    "update",
    "copies",
    "record",
    "record_error",
    "record_without_files",
    "record_version_differs",
    "release_page_url",
    "plugin_page_url",
    "notes",
    "actions",
    "remove_hint",
  ]);
  return {
    name: string(o["name"], `${path}.name`),
    latest: string(o["latest"], `${path}.latest`),
    availability: oneOf(o["availability"], `${path}.availability`, AVAILABILITIES),
    min_os: nullableString(o["min_os"], `${path}.min_os`),
    installed: oneOf(o["installed"], `${path}.installed`, INSTALLED),
    installed_version: nullableString(o["installed_version"], `${path}.installed_version`),
    update: nullableOneOf(o["update"], `${path}.update`, UPDATES),
    copies: array(o["copies"], `${path}.copies`, toCopy),
    record: toRecord(o["record"], `${path}.record`),
    record_error: nullableString(o["record_error"], `${path}.record_error`),
    record_without_files: boolean(o["record_without_files"], `${path}.record_without_files`),
    record_version_differs: boolean(o["record_version_differs"], `${path}.record_version_differs`),
    release_page_url: nullableString(o["release_page_url"], `${path}.release_page_url`),
    plugin_page_url: nullableString(o["plugin_page_url"], `${path}.plugin_page_url`),
    notes: array(o["notes"], `${path}.notes`, toBlock),
    actions: array(o["actions"], `${path}.actions`, (item, at) => oneOf(item, at, ACTION_NAMES)),
    remove_hint: nullableOneOf(o["remove_hint"], `${path}.remove_hint`, REMOVE_HINTS),
  };
}

function toOrphan(value: unknown, path: string): OrphanView {
  const o = object(value, path, ["name", "version"]);
  return { name: string(o["name"], `${path}.name`), version: string(o["version"], `${path}.version`) };
}

/** Checks `value` against FR-046 / FR-051 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toStatusView(value: unknown): StatusView {
  const o = object(value, "$", ["os", "edge_max_age_secs", "running_os", "feed", "plugins", "orphans"]);
  const feed = object(o["feed"], "$.feed", ["source", "fetched_at_unix", "schema_version", "error"]);
  return {
    os: oneOf(o["os"], "$.os", OS_NAMES),
    edge_max_age_secs: count(o["edge_max_age_secs"], "$.edge_max_age_secs"),
    running_os: nullableString(o["running_os"], "$.running_os"),
    feed: {
      source: oneOf(feed["source"], "$.feed.source", FEED_SOURCES),
      fetched_at_unix: nullableCount(feed["fetched_at_unix"], "$.feed.fetched_at_unix"),
      schema_version: nullableCount(feed["schema_version"], "$.feed.schema_version"),
      error: nullableString(feed["error"], "$.feed.error"),
    },
    plugins: array(o["plugins"], "$.plugins", toPlugin),
    orphans: array(o["orphans"], "$.orphans", toOrphan),
  };
}

// What `install_plugin` / `remove_plugin` return and send (spec FR-091, FR-092, FR-097; plan §4.7, PD-11): the
// core's `install::ActionSummary` and `install::InstallProgress`, whose `download` event is `download::Progress`.

export type KeptRoot = "plugin" | "presets";
export type ActionNotice = "rescan_daw" | "rescan_daw_macos";
export type RestartReason =
  | "range_ignored"
  | "range_refused"
  | "partial_too_long"
  | "hash_mismatch_after_resume"
  | "cached_invalid";

/** A path the action kept: its root and a relative path, never an absolute one (FR-091). */
export interface KeptPath {
  readonly root: KeptRoot;
  readonly path: string;
}

export interface ActionSummary {
  readonly name: string;
  readonly action: ActionName;
  readonly version: string | null;
  readonly kept: readonly KeptPath[];
  readonly notice: ActionNotice | null;
}

export type DownloadProgress =
  | { readonly kind: "started"; readonly total: number; readonly resumed_from: number }
  | { readonly kind: "bytes"; readonly received: number; readonly on_disk: number; readonly total: number }
  | { readonly kind: "restarted"; readonly reason: RestartReason }
  | {
      readonly kind: "retrying";
      readonly attempt: number;
      readonly of: number;
      readonly code: string;
      readonly wait_ms: number;
    }
  | { readonly kind: "verifying" }
  | { readonly kind: "verified" };

export type InstallProgress =
  | { readonly kind: "download"; readonly event: DownloadProgress }
  | { readonly kind: "staging" }
  | { readonly kind: "placing"; readonly done: number; readonly total: number }
  | { readonly kind: "committing" }
  | { readonly kind: "removing"; readonly done: number; readonly total: number }
  | { readonly kind: "done" }
  | { readonly kind: "elevating" };

const KEPT_ROOTS: readonly KeptRoot[] = ["plugin", "presets"];
const ACTION_NOTICES: readonly ActionNotice[] = ["rescan_daw", "rescan_daw_macos"];
const RESTART_REASONS: readonly RestartReason[] = [
  "range_ignored",
  "range_refused",
  "partial_too_long",
  "hash_mismatch_after_resume",
  "cached_invalid",
];

function kindOf(value: unknown, path: string): string {
  if (typeof value !== "object" || value === null || Array.isArray(value)) fail(path, "an object");
  return string((value as Readonly<Record<string, unknown>>)["kind"], `${path}.kind`);
}

function toKept(value: unknown, path: string): KeptPath {
  const o = object(value, path, ["root", "path"]);
  return { root: oneOf(o["root"], `${path}.root`, KEPT_ROOTS), path: string(o["path"], `${path}.path`) };
}

function actionSummaryAt(value: unknown, path: string): ActionSummary {
  const o = object(value, path, ["name", "action", "version", "kept", "notice"]);
  return {
    name: string(o["name"], `${path}.name`),
    action: oneOf(o["action"], `${path}.action`, ACTION_NAMES),
    version: nullableString(o["version"], `${path}.version`),
    kept: array(o["kept"], `${path}.kept`, toKept),
    notice: nullableOneOf(o["notice"], `${path}.notice`, ACTION_NOTICES),
  };
}

/** Checks a command's result against FR-091 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toActionSummary(value: unknown): ActionSummary {
  return actionSummaryAt(value, "$");
}

function toDownloadProgress(value: unknown, path: string): DownloadProgress {
  const kind = kindOf(value, path);
  switch (kind) {
    case "started": {
      const o = object(value, path, ["kind", "total", "resumed_from"]);
      return { kind, total: count(o["total"], `${path}.total`), resumed_from: count(o["resumed_from"], `${path}.resumed_from`) };
    }
    case "bytes": {
      const o = object(value, path, ["kind", "received", "on_disk", "total"]);
      return {
        kind,
        received: count(o["received"], `${path}.received`),
        on_disk: count(o["on_disk"], `${path}.on_disk`),
        total: count(o["total"], `${path}.total`),
      };
    }
    case "restarted": {
      const o = object(value, path, ["kind", "reason"]);
      return { kind, reason: oneOf(o["reason"], `${path}.reason`, RESTART_REASONS) };
    }
    case "retrying": {
      const o = object(value, path, ["kind", "attempt", "of", "code", "wait_ms"]);
      return {
        kind,
        attempt: count(o["attempt"], `${path}.attempt`),
        of: count(o["of"], `${path}.of`),
        code: string(o["code"], `${path}.code`),
        wait_ms: count(o["wait_ms"], `${path}.wait_ms`),
      };
    }
    case "verifying":
    case "verified":
      object(value, path, ["kind"]);
      return { kind };
    default:
      return fail(`${path}.kind`, "one of started, bytes, restarted, retrying, verifying, verified");
  }
}

function installProgressAt(value: unknown, path: string): InstallProgress {
  const kind = kindOf(value, path);
  switch (kind) {
    case "download": {
      const o = object(value, path, ["kind", "event"]);
      return { kind, event: toDownloadProgress(o["event"], `${path}.event`) };
    }
    case "placing":
    case "removing": {
      const o = object(value, path, ["kind", "done", "total"]);
      return { kind, done: count(o["done"], `${path}.done`), total: count(o["total"], `${path}.total`) };
    }
    case "staging":
    case "committing":
    case "done":
    case "elevating":
      object(value, path, ["kind"]);
      return { kind };
    default:
      return fail(`${path}.kind`, "one of download, staging, placing, committing, removing, done, elevating");
  }
}

/** Checks one channel message against FR-092 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toInstallProgress(value: unknown): InstallProgress {
  return installProgressAt(value, "$");
}

// What `update_all` returns and sends (Phase 12 FR-001, FR-024, FR-060; plan §4.7, PD-5): the core's
// `install::BatchSummaryView` and `install::BatchProgress`; and what `settings_view` / `settings_save` return
// (FR-040; plan §4.4): `settings::SettingsView`.

export type BatchProgress =
  | { readonly kind: "item"; readonly name: string; readonly event: InstallProgress }
  | { readonly kind: "elevating" };

/** One item's result: exactly one of `summary` / `error` is non-null; `error` begins with its code. */
export interface BatchItemView {
  readonly name: string;
  readonly summary: ActionSummary | null;
  readonly error: string | null;
}

export interface BatchSummary {
  readonly results: readonly BatchItemView[];
  readonly notice: ActionNotice | null;
}

export interface SettingsView {
  readonly os: OsName;
  readonly linux_plugin_root: string;
  readonly root_is_default: boolean;
  readonly graphics_workaround: boolean;
  readonly root_locked_by: readonly string[];
  readonly error: string | null;
}

/** Checks one batch channel message against FR-024 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toBatchProgress(value: unknown): BatchProgress {
  const kind = kindOf(value, "$");
  switch (kind) {
    case "item": {
      const o = object(value, "$", ["kind", "name", "event"]);
      return { kind, name: string(o["name"], "$.name"), event: installProgressAt(o["event"], "$.event") };
    }
    case "elevating":
      object(value, "$", ["kind"]);
      return { kind };
    default:
      return fail("$.kind", "one of item, elevating");
  }
}

function toBatchItem(value: unknown, path: string): BatchItemView {
  const o = object(value, path, ["name", "summary", "error"]);
  const summary = o["summary"] === null ? null : actionSummaryAt(o["summary"], `${path}.summary`);
  const error = nullableString(o["error"], `${path}.error`);
  if ((summary === null) === (error === null)) fail(path, "exactly one of summary and error");
  return { name: string(o["name"], `${path}.name`), summary, error };
}

/** Checks `update_all`'s result against FR-001 / FR-060 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toBatchSummary(value: unknown): BatchSummary {
  const o = object(value, "$", ["results", "notice"]);
  return {
    results: array(o["results"], "$.results", toBatchItem),
    notice: nullableOneOf(o["notice"], "$.notice", ACTION_NOTICES),
  };
}

/** Checks a settings command's result against FR-040 and returns it typed; throws `TypeError("<json path>: <expected>")`. */
export function toSettingsView(value: unknown): SettingsView {
  const o = object(value, "$", [
    "os",
    "linux_plugin_root",
    "root_is_default",
    "graphics_workaround",
    "root_locked_by",
    "error",
  ]);
  return {
    os: oneOf(o["os"], "$.os", OS_NAMES),
    linux_plugin_root: string(o["linux_plugin_root"], "$.linux_plugin_root"),
    root_is_default: boolean(o["root_is_default"], "$.root_is_default"),
    graphics_workaround: boolean(o["graphics_workaround"], "$.graphics_workaround"),
    root_locked_by: array(o["root_locked_by"], "$.root_locked_by", string),
    error: nullableString(o["error"], "$.error"),
  };
}
