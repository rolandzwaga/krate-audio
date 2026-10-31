// The status section (spec FR-070 to FR-078; plan §4.10, PD-13, PD-14, PD-18). Every string comes
// from STATUS_TEXTS or ACTION_TEXTS through `fill` and reaches the DOM through textContent; the only
// properties set from view data are `ol.start` (notes.ts) and `button.disabled`; the release-page URL
// lives only in its button's click closure.
import { renderBlocks } from "./notes";
import type { CopyView, OsName, PluginView, StatusView, UnknownReason } from "./view";

/** Every FR-071 cell, the FR-074 feed lines, the check control and the note: each reached by a golden. */
export const STATUS_TEXTS = {
  installedLine: "Installed: {installed}",
  latestLine: "Latest: {latest}",
  installedNotChecked: "Not checked",
  installedNotInstalled: "Not installed",
  installedElsewhere: "Installed outside the manager's location: {version}",
  installedVersionUnknown: "Installed, version unknown",
  installedVersion: "{version}",
  unknownVersion: "unknown",
  notThroughManager: "Not available through the manager",
  notForThisMac: "Not available for this Mac",
  notForThisComputer: "Not available for this computer",
  needsNewerMacos: "Needs macOS {min_os} or newer",
  needsNewerLinux: "Needs glibc {min_os} or newer",
  needsNewerWindows: "Needs Windows {min_os} or newer",
  updateNotInstalled: "Available to install",
  updateUpToDate: "Up to date",
  updateAvailable: "Update available",
  updateInstalledNewer: "Installed version is newer than the latest release",
  updateUnknown: "Versions cannot be compared",
  copyAu: "AU: ",
  copyFound: "Found: {path}",
  copyVersion: " ({version})",
  copyVersionUnknown: " (version unknown: {reason})",
  copyManaged: " — installer location",
  copyLoadsFirst: " — hosts that follow Steinberg's scan order load this copy first",
  reasonSingleFile: "single-file plug-in",
  reasonNoModuleinfo: "no moduleinfo.json",
  reasonTooLarge: "moduleinfo.json too large",
  reasonUnreadable: "cannot be read",
  reasonCorrupt: "unreadable moduleinfo",
  reasonClassesDisagree: "class versions disagree",
  reasonNoVersion: "no version recorded",
  reasonNotABundle: "not a bundle",
  reasonAuNoVersion: "version not recorded in the AU bundle",
  recordWithoutFiles: "The installer's record lists version {version}, but its files are missing.",
  recordVersionDiffers: "The installer's record says {record}; the installed files say {installed}.",
  recordError: "The installer's record could not be read ({code}).",
  openReleasePage: "Open release page",
  releaseNotes: "Release notes",
  noReleaseNotes: "No release notes.",
  feedFresh: "Checked {time}",
  feedCached: "Could not refresh the list: {error}. Showing the copy from {time}.",
  feedNone: "Could not load the plugin list: {error}",
  checkNow: "Check now",
  checkNote: "A new release can take up to {minutes} minutes to appear.",
} as const satisfies Record<string, string>;

/** The texts no golden can reach (FR-071): asserted by SC-034 and SC-035 instead. */
export const ACTION_TEXTS = {
  pending: "Checking for plugins…",
  checking: "Checking…",
  openFailed: "Could not open the release page: {text}",
} as const satisfies Record<string, string>;

const REASONS: Readonly<Record<UnknownReason, string>> = {
  single_file: STATUS_TEXTS.reasonSingleFile,
  no_moduleinfo: STATUS_TEXTS.reasonNoModuleinfo,
  too_large: STATUS_TEXTS.reasonTooLarge,
  unreadable: STATUS_TEXTS.reasonUnreadable,
  corrupt: STATUS_TEXTS.reasonCorrupt,
  classes_disagree: STATUS_TEXTS.reasonClassesDisagree,
  no_version: STATUS_TEXTS.reasonNoVersion,
  not_a_bundle: STATUS_TEXTS.reasonNotABundle,
  au_no_version: STATUS_TEXTS.reasonAuNoVersion,
};

const NEEDS_NEWER: Readonly<Record<OsName, string>> = {
  macos: STATUS_TEXTS.needsNewerMacos,
  linux: STATUS_TEXTS.needsNewerLinux,
  windows: STATUS_TEXTS.needsNewerWindows,
};

/** `template` with each `{name}` replaced by `values[name]`. */
export function fill(template: string, values: Readonly<Record<string, string>>): string {
  return template.replace(/\{([a-z_]+)\}/g, (match, name: string) => values[name] ?? match);
}

export type Invoker = <T>(cmd: string, args?: Record<string, unknown>) => Promise<T>;

export interface StatusSection {
  readonly element: HTMLElement;
  readonly button: HTMLButtonElement;
  render(view: StatusView): void;
  reject(text: string): void;
}

/** The text of a rejected command: Tauri rejects with the command's `Err(String)`. */
function rejectionText(reason: unknown): string {
  if (typeof reason === "string") return reason;
  if (reason instanceof Error) return reason.message;
  return String(reason);
}

function paragraph(className: string, text: string): HTMLParagraphElement {
  const p = document.createElement("p");
  p.className = className;
  p.textContent = text;
  return p;
}

function installedText(p: PluginView): string {
  const version = p.installed_version ?? STATUS_TEXTS.unknownVersion;
  switch (p.installed) {
    case "not_checked":
      return STATUS_TEXTS.installedNotChecked;
    case "not_installed":
      return STATUS_TEXTS.installedNotInstalled;
    case "elsewhere":
      return fill(STATUS_TEXTS.installedElsewhere, { version });
    case "version_unknown":
      return STATUS_TEXTS.installedVersionUnknown;
    case "version":
      return fill(STATUS_TEXTS.installedVersion, { version });
  }
}

/** FR-071's state line; null when the plugin is available but was not checked (no update state). */
function stateText(os: OsName, p: PluginView): string | null {
  switch (p.availability) {
    case "not_through_manager":
      return STATUS_TEXTS.notThroughManager;
    case "not_for_this_machine":
      return os === "macos" ? STATUS_TEXTS.notForThisMac : STATUS_TEXTS.notForThisComputer;
    case "needs_newer_os":
      return fill(NEEDS_NEWER[os], { min_os: p.min_os ?? "" });
    case "available":
      break;
  }
  switch (p.update) {
    case "not_installed":
      return STATUS_TEXTS.updateNotInstalled;
    case "up_to_date":
      return STATUS_TEXTS.updateUpToDate;
    case "update_available":
      return STATUS_TEXTS.updateAvailable;
    case "installed_newer":
      return STATUS_TEXTS.updateInstalledNewer;
    case "unknown":
      return STATUS_TEXTS.updateUnknown;
    case null:
      return null;
  }
}

function copyText(c: CopyView): string {
  let text = c.format === "au" ? STATUS_TEXTS.copyAu : "";
  text += fill(STATUS_TEXTS.copyFound, { path: c.path });
  if (c.version !== null) text += fill(STATUS_TEXTS.copyVersion, { version: c.version });
  if (c.unknown_reason !== null) text += fill(STATUS_TEXTS.copyVersionUnknown, { reason: REASONS[c.unknown_reason] });
  if (c.managed) text += STATUS_TEXTS.copyManaged;
  if (c.loads_before_managed) text += STATUS_TEXTS.copyLoadsFirst;
  return text;
}

function noticeTexts(p: PluginView): string[] {
  const recorded = p.record?.version ?? STATUS_TEXTS.unknownVersion;
  const notices: string[] = [];
  if (p.record_without_files) notices.push(fill(STATUS_TEXTS.recordWithoutFiles, { version: recorded }));
  if (p.record_version_differs) {
    const installed = p.installed_version ?? STATUS_TEXTS.unknownVersion;
    notices.push(fill(STATUS_TEXTS.recordVersionDiffers, { record: recorded, installed }));
  }
  if (p.record_error !== null) notices.push(fill(STATUS_TEXTS.recordError, { code: p.record_error }));
  return notices;
}

/** The `Open release page` button (FR-078); `url` is held by the click closure only. */
function releaseButton(url: string, invoke: Invoker): HTMLButtonElement {
  const button = document.createElement("button");
  button.className = "open-release";
  button.textContent = STATUS_TEXTS.openReleasePage;
  let error: HTMLParagraphElement | null = null;
  async function open(): Promise<void> {
    if (button.disabled) return;
    button.disabled = true;
    try {
      await invoke<null>("open_release_page", { url });
      error?.remove();
      error = null;
    } catch (reason) {
      const text = fill(ACTION_TEXTS.openFailed, { text: rejectionText(reason) });
      if (error === null) {
        error = paragraph("open-error", text);
        button.after(error);
      } else {
        error.textContent = text;
      }
    } finally {
      button.disabled = false;
    }
  }
  button.addEventListener("click", () => {
    void open();
  });
  return button;
}

function renderCard(os: OsName, p: PluginView, invoke: Invoker): HTMLElement {
  const card = document.createElement("article");
  card.className = "plugin";
  const name = document.createElement("h2");
  name.textContent = p.name;
  card.append(
    name,
    paragraph("installed", fill(STATUS_TEXTS.installedLine, { installed: installedText(p) })),
    paragraph("latest", fill(STATUS_TEXTS.latestLine, { latest: p.latest })),
  );
  const state = stateText(os, p);
  if (state !== null) card.append(paragraph("state", state));
  const copies = document.createElement("ul");
  copies.className = "copies";
  for (const c of p.copies) {
    const item = document.createElement("li");
    item.textContent = copyText(c);
    copies.append(item);
  }
  card.append(copies);
  for (const notice of noticeTexts(p)) card.append(paragraph("record-notice", notice));
  if (p.release_page_url !== null) card.append(releaseButton(p.release_page_url, invoke));
  const details = document.createElement("details");
  details.className = "notes";
  const summary = document.createElement("summary");
  summary.textContent = STATUS_TEXTS.releaseNotes;
  details.append(summary);
  if (p.notes.length === 0) {
    const none = document.createElement("p");
    none.textContent = STATUS_TEXTS.noReleaseNotes;
    details.append(none);
  } else {
    details.append(...renderBlocks(p.notes));
  }
  card.append(details);
  return card;
}

export function renderCards(view: StatusView, invoke: Invoker): HTMLElement[] {
  return view.plugins.map((p) => renderCard(view.os, p, invoke));
}

function timeText(unix: number | null): string {
  return unix === null ? "" : new Date(unix * 1000).toLocaleString();
}

function feedText(view: StatusView): string {
  const error = view.feed.error ?? "";
  const time = timeText(view.feed.fetched_at_unix);
  switch (view.feed.source) {
    case "fresh":
      return fill(STATUS_TEXTS.feedFresh, { time });
    case "cached":
      return fill(STATUS_TEXTS.feedCached, { error, time });
    case "none":
      return fill(STATUS_TEXTS.feedNone, { error });
  }
}

/** The section in its pending state: the pending text and a disabled `Check now` button (FR-070, FR-073). */
export function createStatusSection(invoke: Invoker): StatusSection {
  const element = document.createElement("section");
  element.className = "status";
  const feedLine = paragraph("feed-line", ACTION_TEXTS.pending);
  const check = document.createElement("div");
  check.className = "check";
  const button = document.createElement("button");
  button.className = "check-now";
  button.textContent = STATUS_TEXTS.checkNow;
  button.disabled = true;
  check.append(button);
  const cards = document.createElement("div");
  cards.className = "cards";
  element.append(feedLine, check, cards);
  let note: HTMLParagraphElement | null = null;

  const section: StatusSection = {
    element,
    button,
    render(view) {
      feedLine.textContent = feedText(view);
      const minutes = String(Math.ceil(view.edge_max_age_secs / 60));
      note ??= check.appendChild(paragraph("check-note", ""));
      note.textContent = fill(STATUS_TEXTS.checkNote, { minutes });
      cards.replaceChildren(...renderCards(view, invoke));
    },
    // PD-13: the previous cards and note stay; only the feed line changes.
    reject(text) {
      feedLine.textContent = fill(STATUS_TEXTS.feedNone, { error: text });
    },
  };

  async function checkNow(): Promise<void> {
    if (button.disabled) return;
    button.disabled = true;
    button.textContent = ACTION_TEXTS.checking;
    let view: StatusView | null = null;
    try {
      view = await invoke<StatusView>("check_now");
    } catch (reason) {
      section.reject(rejectionText(reason));
    }
    if (view !== null) section.render(view);
    button.textContent = STATUS_TEXTS.checkNow;
    button.disabled = false;
  }
  button.addEventListener("click", () => {
    void checkNow();
  });
  return section;
}

/** Appends the section before the first `plugin_status` call is awaited (PD-18), then settles it. */
export async function mountStatus(root: HTMLElement, invoke: Invoker): Promise<void> {
  const section = createStatusSection(invoke);
  root.append(section.element);
  let view: StatusView | null = null;
  try {
    view = await invoke<StatusView>("plugin_status");
  } catch (reason) {
    section.reject(rejectionText(reason));
  }
  if (view !== null) section.render(view);
  section.button.disabled = false;
}
