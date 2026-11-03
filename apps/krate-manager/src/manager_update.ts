// The Krate Manager update line (Phase 11 spec FR-024 to FR-026; plan §4.6, PD-25, PD-31). Every string comes from
// UPDATE_TEXTS through `fill` and reaches the DOM through textContent; the notes go through `renderBlocks`; the
// release-page URL lives only in its button's click closure (`releaseButton`).
import { renderBlocks } from "./notes";
import { fill, rejectionText, releaseButton, tauriChannel } from "./status";
import type { ChannelMaker, Invoker } from "./status";
import { toStatusView } from "./view";
import type { Block, OsName } from "./view";

export type UrlRefusal = "api_host" | "latest_path" | "policy" | "host";
export type UpdateAction = { readonly kind: "install_and_restart" } | { readonly kind: "open_release_page"; readonly url: string };

export interface ErrorView {
  readonly code: string;
  readonly sentence: string;
  readonly reason?: UrlRefusal;
  readonly detail?: string;
}

export interface AvailableView {
  readonly version: string;
  readonly date: string | null;
  readonly notes: readonly Block[];
  readonly action: UpdateAction;
}

export interface ManagerUpdateView {
  readonly os: OsName;
  readonly current: string;
  readonly available: AvailableView | null;
  readonly error: ErrorView | null;
}

export interface UpdateLine {
  readonly element: HTMLElement;
  check(): Promise<void>;
}

/** FR-024's texts, SI-31's `.deb` button and the confirmation's two buttons. */
export const UPDATE_TEXTS = {
  available: "Krate Manager {version} is available.",
  button: "Update and restart",
  downloadDeb: "Download the new .deb",
  confirm: "Download Krate Manager {version}, verify its signature, then close and restart. Plugin installs must be finished first.",
  confirmMac: "macOS may ask for your administrator password.",
  downloading: "Downloading and verifying…",
  installing: "Installing… Krate Manager will restart.",
  checkFailed: "Could not check for a Krate Manager update ({code}).",
  yes: "Confirm",
  no: "Cancel",
} as const satisfies Record<string, string>;

const OS_NAMES: readonly OsName[] = ["windows", "macos", "linux"];
const URL_REFUSALS: readonly UrlRefusal[] = ["api_host", "latest_path", "policy", "host"];

function fail(path: string, expected: string): never {
  throw new TypeError(`${path}: ${expected}`);
}

/** The object at `path`: every key of `required` present, no key outside `required` and `optional`. */
function object(
  value: unknown,
  path: string,
  required: readonly string[],
  optional: readonly string[] = [],
): Readonly<Record<string, unknown>> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) fail(path, "an object");
  const record = value as Readonly<Record<string, unknown>>;
  for (const key of required) if (!(key in record)) fail(`${path}.${key}`, "present");
  for (const key of Object.keys(record)) {
    if (!required.includes(key) && !optional.includes(key)) fail(`${path}.${key}`, "no such key");
  }
  return record;
}

function string(value: unknown, path: string): string {
  return typeof value === "string" ? value : fail(path, "a string");
}

function oneOf<T extends string>(value: unknown, path: string, allowed: readonly T[]): T {
  const found = allowed.find((candidate) => candidate === value);
  return found ?? fail(path, `one of ${allowed.join(", ")}`);
}

/**
 * The notes through view.ts's own block checks (FR-072's model): view.ts keeps its block parser private, so the
 * notes are checked as the notes of a one-card status view, unchanged.
 */
function toNotes(value: unknown): readonly Block[] {
  const plugin = {
    name: "",
    latest: "",
    availability: "available",
    min_os: null,
    installed: "not_checked",
    installed_version: null,
    update: null,
    copies: [],
    record: null,
    record_error: null,
    record_without_files: false,
    record_version_differs: false,
    release_page_url: null,
    plugin_page_url: null,
    description: null,
    notes: value,
    actions: [],
    remove_hint: null,
  };
  const feed = { source: "none", fetched_at_unix: null, schema_version: null, error: null };
  const view = toStatusView({ os: "linux", edge_max_age_secs: 0, running_os: null, feed, plugins: [plugin], orphans: [] });
  return view.plugins[0]?.notes ?? fail("$.available.notes", "an array of blocks");
}

/** PD-31: exactly the two shapes the core sends. */
function toAction(value: unknown, path: string): UpdateAction {
  if (typeof value !== "object" || value === null || Array.isArray(value)) fail(path, "an object");
  const kind = (value as Readonly<Record<string, unknown>>)["kind"];
  switch (kind) {
    case "install_and_restart":
      object(value, path, ["kind"]);
      return { kind };
    case "open_release_page": {
      const o = object(value, path, ["kind", "url"]);
      return { kind, url: string(o["url"], `${path}.url`) };
    }
    default:
      return fail(`${path}.kind`, "one of install_and_restart, open_release_page");
  }
}

function toAvailable(value: unknown, path: string): AvailableView | null {
  if (value === null) return null;
  const o = object(value, path, ["version", "date", "notes", "action"]);
  return {
    version: string(o["version"], `${path}.version`),
    date: o["date"] === null ? null : string(o["date"], `${path}.date`),
    notes: toNotes(o["notes"]),
    action: toAction(o["action"], `${path}.action`),
  };
}

/** Tasks TA-21's `ErrorView`: `reason` and `detail` are present only when the variant has them. */
function toErrorView(value: unknown, path: string): ErrorView {
  const o = object(value, path, ["code", "sentence"], ["reason", "detail"]);
  const view: { code: string; sentence: string; reason?: UrlRefusal; detail?: string } = {
    code: string(o["code"], `${path}.code`),
    sentence: string(o["sentence"], `${path}.sentence`),
  };
  if ("reason" in o) view.reason = oneOf(o["reason"], `${path}.reason`, URL_REFUSALS);
  if ("detail" in o) view.detail = string(o["detail"], `${path}.detail`);
  return view;
}

/** Checks `manager_update_check`'s result against FR-014 and returns it typed; throws `TypeError("<path>: <expected>")`. */
export function toManagerUpdateView(value: unknown): ManagerUpdateView {
  const o = object(value, "$", ["os", "current", "available", "error"]);
  return {
    os: oneOf(o["os"], "$.os", OS_NAMES),
    current: string(o["current"], "$.current"),
    available: toAvailable(o["available"], "$.available"),
    error: o["error"] === null ? null : toErrorView(o["error"], "$.error"),
  };
}

type Progress = "downloading" | "installing";

/** PD-25's channel message, or null when it fails the parser (and so changes nothing). */
function toProgress(message: unknown): Progress | null {
  try {
    const o = object(message, "$", ["event"]);
    return oneOf(o["event"], "$.event", ["downloading", "installing"] as const);
  } catch {
    return null;
  }
}

/** A failure line starting with its code (FR-024): the serialized `ErrorView`, else the rejection's own text. */
function failureText(reason: unknown): string {
  const text = rejectionText(reason);
  try {
    const view = toErrorView(JSON.parse(text), "$");
    return `${view.code}: ${view.sentence}`;
  } catch {
    return text;
  }
}

function paragraph(className: string, text: string): HTMLParagraphElement {
  const p = document.createElement("p");
  p.className = className;
  p.textContent = text;
  return p;
}

function button(className: string, text: string): HTMLButtonElement {
  const b = document.createElement("button");
  b.className = className;
  b.textContent = text;
  return b;
}

/**
 * The header's update line (FR-024): empty until a check announces an update, one muted line when the check failed.
 * `actions` is the status section's switch for every plugin action (FR-025): off from the confirmation until the
 * install fails.
 */
export function createUpdateLine(
  invoke: Invoker,
  actions: { setEnabled(enabled: boolean): void },
  makeChannel: ChannelMaker = tauriChannel,
): UpdateLine {
  const element = document.createElement("div");
  element.className = "manager-update";
  let installing = false;

  /** Nothing while an install runs: its progress and failure lines stay (FR-025). */
  function render(view: ManagerUpdateView | null): void {
    if (installing) return;
    if (view === null) {
      element.replaceChildren(); // a rejected check, or a view that fails the parser, shows nothing
      return;
    }
    if (view.error !== null) {
      element.replaceChildren(paragraph("update-muted", fill(UPDATE_TEXTS.checkFailed, { code: view.error.code })));
      return;
    }
    const available = view.available;
    if (available === null) {
      element.replaceChildren();
      return;
    }
    const notes = document.createElement("div");
    notes.className = "update-notes";
    notes.append(...renderBlocks(available.notes));
    const action =
      available.action.kind === "open_release_page"
        ? releaseButton(available.action.url, invoke, UPDATE_TEXTS.downloadDeb)
        : restartButton(view.os, available.version);
    element.replaceChildren(paragraph("update-available", fill(UPDATE_TEXTS.available, { version: available.version })), notes, action);
  }

  /** "Update and restart": the in-page confirmation (never `window.confirm`), then the install. */
  function restartButton(os: OsName, version: string): HTMLButtonElement {
    const restart = button("update-restart", UPDATE_TEXTS.button);
    restart.addEventListener("click", () => {
      if (restart.disabled) return;
      const confirm = document.createElement("div");
      confirm.className = "update-confirm";
      confirm.append(paragraph("update-confirm-text", fill(UPDATE_TEXTS.confirm, { version })));
      if (os === "macos") confirm.append(paragraph("update-admin-note", UPDATE_TEXTS.confirmMac));
      const yes = button("update-yes", UPDATE_TEXTS.yes);
      const no = button("update-no", UPDATE_TEXTS.no);
      confirm.append(yes, no);
      no.addEventListener("click", () => {
        confirm.replaceWith(restart);
      });
      yes.addEventListener("click", () => {
        confirm.replaceWith(restart);
        void install(restart);
      });
      restart.replaceWith(confirm);
    });
    return restart;
  }

  async function install(restart: HTMLButtonElement): Promise<void> {
    installing = true;
    restart.disabled = true;
    actions.setEnabled(false);
    for (const line of Array.from(element.querySelectorAll(":scope > p.update-failure"))) line.remove();
    const progress = paragraph("update-progress", UPDATE_TEXTS.downloading);
    restart.after(progress);
    let stage: Progress = "downloading";
    const onProgress = makeChannel((message) => {
      const next = toProgress(message);
      // The text never moves back from installing to downloading.
      if (next === null || stage === "installing") return;
      stage = next;
      progress.textContent = next === "installing" ? UPDATE_TEXTS.installing : UPDATE_TEXTS.downloading;
    });
    try {
      await invoke<null>("manager_update_install", { onProgress });
      // Success: the updater restarts the app (FR-016); the line keeps the installing text until then.
    } catch (reason) {
      progress.replaceWith(paragraph("update-failure", failureText(reason)));
      restart.disabled = false;
      installing = false;
      actions.setEnabled(true);
    }
  }

  async function check(): Promise<void> {
    // A check replaces the kept update (FR-023): never while one is being installed.
    if (installing) return;
    let view: ManagerUpdateView | null;
    try {
      view = toManagerUpdateView(await invoke<unknown>("manager_update_check"));
    } catch {
      view = null;
    }
    render(view);
  }

  return { element, check };
}
