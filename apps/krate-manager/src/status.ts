// The status section (spec FR-070 to FR-078, FR-097 to FR-099, Phase 10 FR-130 to FR-135; plan §4.7, §4.10, PD-13,
// PD-14, PD-18, PD-22). Every string comes from STATUS_TEXTS, ACTION_TEXTS or INSTALL_TEXTS through `fill` and reaches
// the DOM through textContent; the only properties set from view data are `ol.start` (notes.ts) and
// `button.disabled`; the release-page URL lives only in its button's click closure.
import { Channel } from "@tauri-apps/api/core";
import { renderBlocks } from "./notes";
import { toActionSummary, toInstallProgress } from "./view";
import type { ActionName, ActionSummary, CopyView, InstallProgress, OsName, PluginView, StatusView, UnknownReason } from "./view";

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

/** The FR-097 to FR-099 action texts: buttons, the in-card confirmation, progress and the outcome lines. */
export const INSTALL_TEXTS = {
  install: "Install",
  update: "Update",
  remove: "Remove",
  confirm: "Confirm",
  cancel: "Cancel",
  confirmInstall: "Install {name} {latest}?",
  confirmUpdate: "Update {name} {installed} to {latest}?",
  confirmRemove: "Remove {name}? The plug-in and its factory presets are removed; your own presets are kept.",
  otherCopy: "Another copy is installed at {path}. Remove it by hand; the manager does not touch it.",
  downloading: "Downloading {done} of {total} bytes",
  placing: "Placing {done} of {total} files",
  removing: "Removing {done} of {total} files",
  finishing: "Finishing",
  installed: "Installed {version}",
  updated: "Updated to {version}",
  removed: "Removed",
  rescan: "Restart your DAW or rescan its plug-ins to load the new version.",
  kept: "Kept {path}: changed since install",
  // Phase 10 FR-131 to FR-134: the Windows / macOS confirmation lines, the elevation progress, the AU steps in Apple's
  // order (the guide is plain text: the webview opens no remote content) and the outside-root hint.
  adminWindows: "Windows will ask for administrator permission once.",
  adminMacos: "macOS will ask for your administrator password once.",
  confirmRemoveInstaller:
    "Remove {name}? Its installer's uninstaller removes the plug-in and its factory presets; your own presets are kept.",
  elevating: "Waiting for administrator permission and the installer…",
  rescanMacos1: "If a Logic or GarageBand AU does not appear: 1. Restart the Mac.",
  rescanMacos2: "2. In Logic's Plug-in Manager, use 'Reset & Rescan Selection'.",
  rescanMacos3: "3. Still missing: use 'Full Audio Unit Reset', then reopen Logic.",
  rescanMacos4:
    "4. Last resort: move the files in ~/Library/Caches/AudioUnitCache out, restart, and reopen Logic (the rescan can take a while).",
  appleGuide: "Apple's guide: support.apple.com/en-us/122179",
  removeElsewhere: "Installed outside the standard folder: uninstall it from Windows Settings → Apps.",
} as const satisfies Record<string, string>;

/** FR-131's administrator line per OS; Linux actions run unelevated and add none. */
const ADMIN_LINES: Readonly<Record<OsName, string | null>> = {
  windows: INSTALL_TEXTS.adminWindows,
  macos: INSTALL_TEXTS.adminMacos,
  linux: null,
};

const ACTION_LABELS: Readonly<Record<ActionName, string>> = {
  install: INSTALL_TEXTS.install,
  update: INSTALL_TEXTS.update,
  remove: INSTALL_TEXTS.remove,
};

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

/**
 * Makes the `onProgress` argument of an action command: a Tauri `Channel` in the app, a fake in tests (PD-22).
 * Messages arrive as `unknown` and go through `toInstallProgress` (plan §4.7's `<T>` form fails the lint's
 * no-unnecessary-type-parameters).
 */
export type ChannelMaker = (onMessage: (message: unknown) => void) => unknown;

export const tauriChannel: ChannelMaker = (onMessage) => {
  const channel = new Channel<unknown>();
  channel.onmessage = onMessage;
  return channel;
};

/** What a card's actions need from the cards' owner (FR-098). */
interface CardHost {
  readonly invoke: Invoker;
  readonly makeChannel: ChannelMaker;
  /** The outcome lines per plugin name, shown on every render until that plugin's next action or `Check now`. */
  readonly outcomes: Map<string, string[]>;
  /** Re-reads the status after an action; the section re-renders, bare `renderCards` has nothing to re-render. */
  refresh(): Promise<void>;
  /** False while Krate Manager updates itself (Phase 11 FR-025): every action button stays disabled. */
  actionsEnabled(): boolean;
}

export interface StatusSection {
  readonly element: HTMLElement;
  readonly button: HTMLButtonElement;
  render(view: StatusView): void;
  reject(text: string): void;
  /** Phase 11 FR-025: disables or re-enables every card action button and `Check now`. */
  setActionsEnabled(enabled: boolean): void;
}

/** The text of a rejected command: Tauri rejects with the command's `Err(String)`. */
export function rejectionText(reason: unknown): string {
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

/**
 * The `Open release page` button (FR-078); `url` is held by the click closure only. Phase 11 PD-31: the update line's
 * "Download the new .deb" is this button with its own `label`.
 */
export function releaseButton(url: string, invoke: Invoker, label: string = STATUS_TEXTS.openReleasePage): HTMLButtonElement {
  const button = document.createElement("button");
  button.className = "open-release";
  button.textContent = label;
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

function button(className: string, text: string): HTMLButtonElement {
  const b = document.createElement("button");
  b.className = className;
  b.textContent = text;
  return b;
}

/**
 * FR-099's one-item confirmation; for Install over an `elsewhere` copy also the other copy's path (FR-124). On Windows
 * and macOS Remove names the installer's uninstaller and every action ends with the administrator line (FR-131).
 */
function confirmLines(os: OsName, p: PluginView, action: ActionName): HTMLParagraphElement[] {
  const lines: HTMLParagraphElement[] = [];
  switch (action) {
    case "install": {
      lines.push(paragraph("confirm-text", fill(INSTALL_TEXTS.confirmInstall, { name: p.name, latest: p.latest })));
      const other = p.installed === "elsewhere" ? p.copies.find((c) => c.format === "vst3" && !c.managed) : undefined;
      if (other !== undefined) lines.push(paragraph("other-copy", fill(INSTALL_TEXTS.otherCopy, { path: other.path })));
      break;
    }
    case "update": {
      const installed = p.installed_version ?? STATUS_TEXTS.unknownVersion;
      lines.push(paragraph("confirm-text", fill(INSTALL_TEXTS.confirmUpdate, { name: p.name, installed, latest: p.latest })));
      break;
    }
    case "remove": {
      const template = os === "linux" ? INSTALL_TEXTS.confirmRemove : INSTALL_TEXTS.confirmRemoveInstaller;
      lines.push(paragraph("confirm-text", fill(template, { name: p.name })));
      break;
    }
  }
  const admin = ADMIN_LINES[os];
  if (admin !== null) lines.push(paragraph("admin-note", admin));
  return lines;
}

/** FR-097's progress text for one event; null for the events that change nothing on screen. */
function progressText(event: InstallProgress): string | null {
  switch (event.kind) {
    case "download":
      return event.event.kind === "bytes"
        ? fill(INSTALL_TEXTS.downloading, { done: String(event.event.on_disk), total: String(event.event.total) })
        : null;
    case "staging":
      return null;
    case "elevating":
      return INSTALL_TEXTS.elevating;
    case "placing":
      return fill(INSTALL_TEXTS.placing, { done: String(event.done), total: String(event.total) });
    case "removing":
      return fill(INSTALL_TEXTS.removing, { done: String(event.done), total: String(event.total) });
    case "committing":
    case "done":
      return INSTALL_TEXTS.finishing;
  }
}

/** FR-098's outcome lines of a successful action; `rescan_daw_macos` adds Apple's AU steps and guide (FR-133). */
function outcomeLines(summary: ActionSummary): string[] {
  const version = summary.version ?? STATUS_TEXTS.unknownVersion;
  const lines: string[] = [];
  switch (summary.action) {
    case "install":
      lines.push(fill(INSTALL_TEXTS.installed, { version }));
      break;
    case "update":
      lines.push(fill(INSTALL_TEXTS.updated, { version }));
      break;
    case "remove":
      lines.push(INSTALL_TEXTS.removed);
      break;
  }
  if (summary.notice !== null) lines.push(INSTALL_TEXTS.rescan);
  if (summary.notice === "rescan_daw_macos") {
    lines.push(
      INSTALL_TEXTS.rescanMacos1,
      INSTALL_TEXTS.rescanMacos2,
      INSTALL_TEXTS.rescanMacos3,
      INSTALL_TEXTS.rescanMacos4,
      INSTALL_TEXTS.appleGuide,
    );
  }
  for (const kept of summary.kept) lines.push(fill(INSTALL_TEXTS.kept, { path: kept.path }));
  return lines;
}

/**
 * The card's `div.actions` row (FR-097 to FR-099): a click swaps it for the in-card confirmation; `Confirm` disables
 * every action button, shows progress in front of `anchor`, runs the command, then the outcome lines and the refresh.
 */
function actionRow(os: OsName, card: HTMLElement, anchor: HTMLElement, p: PluginView, host: CardHost): HTMLDivElement {
  const row = document.createElement("div");
  row.className = "actions";
  const buttons = shownActions(p).map((action) => {
    const b = button(`action-${action}`, ACTION_LABELS[action]);
    b.disabled = !host.actionsEnabled();
    b.addEventListener("click", () => {
      if (!b.disabled) ask(action);
    });
    return b;
  });
  row.append(...buttons);

  function ask(action: ActionName): void {
    const confirm = document.createElement("div");
    confirm.className = "confirm";
    const ok = button("confirm-ok", INSTALL_TEXTS.confirm);
    const cancel = button("confirm-cancel", INSTALL_TEXTS.cancel);
    confirm.append(...confirmLines(os, p, action), ok, cancel);
    cancel.addEventListener("click", () => {
      // The row was detached while asking: a self-update may have started or ended meanwhile (FR-025).
      for (const b of buttons) b.disabled = !host.actionsEnabled();
      confirm.replaceWith(row);
    });
    ok.addEventListener("click", () => {
      if (ok.disabled) return;
      confirm.replaceWith(row);
      void run(action);
    });
    row.replaceWith(confirm);
  }

  async function run(action: ActionName): Promise<void> {
    host.outcomes.delete(p.name);
    for (const line of Array.from(card.querySelectorAll(":scope > p.outcome"))) line.remove();
    // `running` keeps `setActionsEnabled(true)` from re-enabling this row before its command settles.
    row.classList.add("running");
    for (const b of buttons) b.disabled = true;
    const progress = paragraph("progress", "");
    const onProgress = host.makeChannel((message) => {
      let text: string | null;
      try {
        text = progressText(toInstallProgress(message));
      } catch {
        return; // An event that fails its parser changes nothing; the command's result still settles the card.
      }
      if (text === null) return;
      progress.textContent = text;
      if (progress.parentNode === null) anchor.before(progress);
    });
    let lines: string[];
    try {
      const result =
        action === "remove"
          ? await host.invoke<unknown>("remove_plugin", { name: p.name, onProgress })
          : await host.invoke<unknown>("install_plugin", { name: p.name, expectedVersion: p.latest, action, onProgress });
      lines = outcomeLines(toActionSummary(result));
    } catch (reason) {
      lines = [rejectionText(reason)];
    }
    progress.remove();
    host.outcomes.set(p.name, lines);
    anchor.before(...lines.map((line) => paragraph("outcome", line)));
    await host.refresh();
    row.classList.remove("running");
    for (const b of buttons) b.disabled = !host.actionsEnabled();
  }

  return row;
}

/** The core's `actions`, without Remove on a card the user removes from Windows Settings (FR-134). */
function shownActions(p: PluginView): readonly ActionName[] {
  return p.remove_hint === null ? p.actions : p.actions.filter((action) => action !== "remove");
}

function renderCard(os: OsName, p: PluginView, host: CardHost): HTMLElement {
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
  if (p.remove_hint !== null) card.append(paragraph("remove-hint", INSTALL_TEXTS.removeElsewhere));
  if (p.release_page_url !== null) card.append(releaseButton(p.release_page_url, host.invoke));
  const details = document.createElement("details");
  if (shownActions(p).length > 0) card.append(actionRow(os, card, details, p, host));
  for (const line of host.outcomes.get(p.name) ?? []) card.append(paragraph("outcome", line));
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

function renderCardsWith(view: StatusView, host: CardHost): HTMLElement[] {
  return view.plugins.map((p) => renderCard(view.os, p, host));
}

export function renderCards(view: StatusView, invoke: Invoker, makeChannel: ChannelMaker = tauriChannel): HTMLElement[] {
  return renderCardsWith(view, {
    invoke,
    makeChannel,
    outcomes: new Map(),
    refresh: () => Promise.resolve(),
    actionsEnabled: () => true,
  });
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

/** What `mountStatus` and `Check now` call once a status read settles (Phase 11 FR-026: the manager's update check). */
export type AfterSettle = () => Promise<void>;

const nothingAfter: AfterSettle = () => Promise.resolve();

/**
 * The section in its pending state: the pending text and a disabled `Check now` button (FR-070, FR-073); `afterSettle`
 * runs after each `check_now` settles, success or rejection (Phase 11 FR-024).
 */
export function createStatusSection(
  invoke: Invoker,
  makeChannel: ChannelMaker = tauriChannel,
  afterSettle: AfterSettle = nothingAfter,
): StatusSection {
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
  let actionsEnabled = true;
  let checking = false;
  // FR-098: after an action the section re-reads the status and re-renders, keeping the outcome lines.
  const host: CardHost = {
    invoke,
    makeChannel,
    outcomes: new Map(),
    actionsEnabled: () => actionsEnabled,
    async refresh() {
      let view: StatusView | null = null;
      try {
        view = await invoke<StatusView>("plugin_status");
      } catch (reason) {
        section.reject(rejectionText(reason));
      }
      if (view !== null) section.render(view);
    },
  };

  const section: StatusSection = {
    element,
    button,
    render(view) {
      feedLine.textContent = feedText(view);
      const minutes = String(Math.ceil(view.edge_max_age_secs / 60));
      note ??= check.appendChild(paragraph("check-note", ""));
      note.textContent = fill(STATUS_TEXTS.checkNote, { minutes });
      cards.replaceChildren(...renderCardsWith(view, host));
    },
    // PD-13: the previous cards and note stay; only the feed line changes.
    reject(text) {
      feedLine.textContent = fill(STATUS_TEXTS.feedNone, { error: text });
    },
    setActionsEnabled(enabled) {
      actionsEnabled = enabled;
      const selector = "div.actions:not(.running) > button, div.confirm > button.confirm-ok";
      for (const b of Array.from(cards.querySelectorAll<HTMLButtonElement>(selector))) b.disabled = !enabled;
      button.disabled = !enabled || checking;
    },
  };

  async function checkNow(): Promise<void> {
    if (button.disabled) return;
    button.disabled = true;
    checking = true;
    button.textContent = ACTION_TEXTS.checking;
    host.outcomes.clear();
    let view: StatusView | null = null;
    try {
      view = await invoke<StatusView>("check_now");
    } catch (reason) {
      section.reject(rejectionText(reason));
    }
    if (view !== null) section.render(view);
    button.textContent = STATUS_TEXTS.checkNow;
    checking = false;
    button.disabled = !actionsEnabled;
    await afterSettle();
  }
  button.addEventListener("click", () => {
    void checkNow();
  });
  return section;
}

/**
 * Appends the section before the first `plugin_status` call is awaited (PD-18), then settles it and runs `afterSettle`,
 * whether that call succeeded or was rejected (Phase 11 FR-026). `onMount` receives the section once it is appended.
 */
export async function mountStatus(
  root: HTMLElement,
  invoke: Invoker,
  makeChannel: ChannelMaker = tauriChannel,
  afterSettle: AfterSettle = nothingAfter,
  onMount: (section: StatusSection) => void = () => undefined,
): Promise<void> {
  const section = createStatusSection(invoke, makeChannel, afterSettle);
  root.append(section.element);
  onMount(section);
  let view: StatusView | null = null;
  try {
    view = await invoke<StatusView>("plugin_status");
  } catch (reason) {
    section.reject(rejectionText(reason));
  }
  if (view !== null) section.render(view);
  section.button.disabled = false;
  await afterSettle();
}
