// TypeScript 6 defaults `types` to [], so tsconfig.json does not see @types/node; the SC-034 source scan needs node:fs.
/// <reference types="node" />
import { afterEach, expect, test } from "vitest";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { invoke } from "@tauri-apps/api/core";
import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";
import cachedStale from "../tests/fixtures/status/cached-stale.json";
import freshAllStates from "../tests/fixtures/status/fresh-all-states.json";
import futureSchema from "../tests/fixtures/status/future-schema.json";
import installedElsewhere from "../tests/fixtures/status/installed-elsewhere.json";
import macosAuCopies from "../tests/fixtures/status/macos-au-copies.json";
import macosIntelNotForThisMac from "../tests/fixtures/status/macos-intel-not-for-this-mac.json";
import needsNewerOsLinux from "../tests/fixtures/status/needs-newer-os-linux.json";
import needsNewerOsMacos from "../tests/fixtures/status/needs-newer-os-macos.json";
import needsNewerOsWindows from "../tests/fixtures/status/needs-newer-os-windows.json";
import noFeed from "../tests/fixtures/status/no-feed.json";
import notForThisComputerLinux from "../tests/fixtures/status/not-for-this-computer-linux.json";
import notesRendering from "../tests/fixtures/status/notes-rendering.json";
import recordNotices from "../tests/fixtures/status/record-notices.json";
import { renderShell } from "./main";
import { STATUS_TEXTS, createStatusSection, renderCards } from "./status";
import type { ChannelMaker, Invoker, StatusSection } from "./status";
import { toStatusView } from "./view";
import type { CopyView, PluginView, StatusView, UnknownReason } from "./view";

afterEach(clearMocks);

/** The 13 SC-012 goldens, each through the runtime validator (PD-12). */
const GOLDENS: readonly (readonly [string, unknown])[] = [
  ["fresh-all-states", freshAllStates],
  ["cached-stale", cachedStale],
  ["no-feed", noFeed],
  ["future-schema", futureSchema],
  ["macos-intel-not-for-this-mac", macosIntelNotForThisMac],
  ["not-for-this-computer-linux", notForThisComputerLinux],
  ["needs-newer-os-macos", needsNewerOsMacos],
  ["needs-newer-os-linux", needsNewerOsLinux],
  ["needs-newer-os-windows", needsNewerOsWindows],
  ["macos-au-copies", macosAuCopies],
  ["record-notices", recordNotices],
  ["notes-rendering", notesRendering],
  ["installed-elsewhere", installedElsewhere],
];

// The FR-071 / FR-074 strings, written here as literals so the renderer's table is checked, not copied.
const REASONS: Readonly<Record<UnknownReason, string>> = {
  single_file: "single-file plug-in",
  no_moduleinfo: "no moduleinfo.json",
  too_large: "moduleinfo.json too large",
  unreadable: "cannot be read",
  corrupt: "unreadable moduleinfo",
  classes_disagree: "class versions disagree",
  no_version: "no version recorded",
  not_a_bundle: "not a bundle",
  au_no_version: "version not recorded in the AU bundle",
};

function installedText(p: PluginView): string {
  switch (p.installed) {
    case "not_checked":
      return "Not checked";
    case "not_installed":
      return "Not installed";
    case "elsewhere":
      return `Installed outside the manager's location: ${p.installed_version ?? "unknown"}`;
    case "version_unknown":
      return "Installed, version unknown";
    case "version":
      return p.installed_version ?? "unknown";
  }
}

function stateText(view: StatusView, p: PluginView): string | null {
  const os = { macos: "macOS", linux: "glibc", windows: "Windows" }[view.os];
  switch (p.availability) {
    case "not_through_manager":
      return "Not available through the manager";
    case "not_for_this_machine":
      return view.os === "macos" ? "Not available for this Mac" : "Not available for this computer";
    case "needs_newer_os":
      return `Needs ${os} ${p.min_os ?? ""} or newer`;
    case "available":
      break;
  }
  switch (p.update) {
    case "not_installed":
      return "Available to install";
    case "up_to_date":
      return "Up to date";
    case "update_available":
      return "Update available";
    case "installed_newer":
      return "Installed version is newer than the latest release";
    case "unknown":
      return "Versions cannot be compared";
    case null:
      return null;
  }
}

function copyText(c: CopyView): string {
  let text = `${c.format === "au" ? "AU: " : ""}Found: ${c.path}`;
  if (c.version !== null) text += ` (${c.version})`;
  if (c.unknown_reason !== null) text += ` (version unknown: ${REASONS[c.unknown_reason]})`;
  if (c.managed) text += " — installer location";
  if (c.loads_before_managed) text += " — hosts that follow Steinberg's scan order load this copy first";
  return text;
}

function noticeTexts(p: PluginView): string[] {
  const out: string[] = [];
  const recorded = p.record?.version ?? "unknown";
  if (p.record_without_files) out.push(`The installer's record lists version ${recorded}, but its files are missing.`);
  if (p.record_version_differs) {
    out.push(`The installer's record says ${recorded}; the installed files say ${p.installed_version ?? "unknown"}.`);
  }
  if (p.record_error !== null) out.push(`The installer's record could not be read (${p.record_error}).`);
  return out;
}

function timeText(unix: number | null): string {
  return unix === null ? "" : new Date(unix * 1000).toLocaleString();
}

function feedText(view: StatusView): string {
  const error = view.feed.error ?? "";
  switch (view.feed.source) {
    case "fresh":
      return `Checked ${timeText(view.feed.fetched_at_unix)}`;
    case "cached":
      return `Could not refresh the list: ${error}. Showing the copy from ${timeText(view.feed.fetched_at_unix)}.`;
    case "none":
      return `Could not load the plugin list: ${error}`;
  }
}

function texts(root: ParentNode, selector: string): string[] {
  return Array.from(root.querySelectorAll(selector), (e) => e.textContent);
}

function only<T extends Element>(root: ParentNode, selector: string, kind: new () => T): T {
  const found = root.querySelectorAll(selector);
  expect(found.length, selector).toBe(1);
  const element = found[0];
  if (!(element instanceof kind)) throw new Error(`${selector} is not a ${kind.name}`);
  return element;
}

function card(section: ParentNode, name: string): HTMLElement {
  const match = Array.from(section.querySelectorAll<HTMLElement>("article.plugin")).find(
    (a) => a.querySelector("h2")?.textContent === name,
  );
  if (match === undefined) throw new Error(`no card for ${name}`);
  return match;
}

/** FR-075: every element's attributes are a subset of class, start (ol), disabled (button); no open on a fresh render. */
function attributeFindings(root: Element): string[] {
  const findings: string[] = [];
  for (const element of [root, ...Array.from(root.querySelectorAll("*"))]) {
    for (const attribute of Array.from(element.attributes)) {
      const tag = element.tagName.toLowerCase();
      const allowed =
        attribute.name === "class" ||
        (attribute.name === "start" && tag === "ol") ||
        (attribute.name === "disabled" && tag === "button");
      if (!allowed) findings.push(`${tag}[${attribute.name}]`);
    }
  }
  return findings;
}

function renderSection(view: StatusView): HTMLElement {
  const section = createStatusSection(invoke);
  section.render(view);
  return section.element;
}

/** Scenario-specific checks beyond the table (plan §7.1 row 019). */
const EXTRA: Readonly<Record<string, (section: HTMLElement, view: StatusView) => void>> = {
  "notes-rendering": (section, view) => {
    const alpha = only(card(section, "Alpha"), "details.notes", HTMLDetailsElement);
    const children = Array.from(alpha.children);
    expect(children[0]?.tagName.toLowerCase()).toBe("summary");
    expect(children.slice(1).map((e) => e.tagName.toLowerCase())).toEqual(["h3", "h6", "ol", "ul", "blockquote", "pre", "hr", "p"]);
    const ol = children[3];
    if (!(ol instanceof HTMLOListElement)) throw new Error("no ol");
    expect(ol.start).toBe(3);
    expect(children[4]?.querySelector(":scope > li > ul")).not.toBeNull();
    const pre = children[6];
    expect(pre?.children.length).toBe(1);
    expect(pre?.children[0]?.tagName.toLowerCase()).toBe("code");
    const code = view.plugins.find((p) => p.name === "Alpha")?.notes.find((b) => b.type === "code");
    expect(code?.type === "code" ? code.text : null).toContain("let x = 1;");
    expect(pre?.textContent).toBe(code?.type === "code" ? code.text : null);
    const p = children[8];
    for (const tag of ["strong", "em", "code", "br"]) expect(p?.querySelector(tag), tag).not.toBeNull();
    const bravo = only(card(section, "Bravo"), "details.notes", HTMLDetailsElement);
    expect(Array.from(bravo.children, (e) => e.tagName.toLowerCase())).toEqual(["summary", "p"]);
    expect(bravo.children[1]?.textContent).toBe("No release notes.");
  },
  "installed-elsewhere": (_section, view) => {
    const elsewhere = view.plugins.filter((p) => p.installed === "elsewhere");
    expect(elsewhere.some((p) => p.installed_version !== null)).toBe(true);
    expect(elsewhere.some((p) => p.installed_version === null)).toBe(true);
    expect(view.plugins.some((p) => p.copies.some((c) => c.nested))).toBe(true);
  },
};

for (const [scenario, golden] of GOLDENS) {
  test(`renders ${scenario}`, () => {
    const view = toStatusView(golden);
    const section = renderSection(view);
    expect(section.matches("section.status")).toBe(true);
    expect(only(section, "p.feed-line", HTMLParagraphElement).textContent).toBe(feedText(view));
    const button = only(section, "button.check-now", HTMLButtonElement);
    expect(button.textContent).toBe("Check now");
    expect(only(section, "p.check-note", HTMLParagraphElement).textContent).toBe(
      `A new release can take up to ${String(Math.ceil(view.edge_max_age_secs / 60))} minutes to appear.`,
    );
    const cards = section.querySelectorAll("article.plugin");
    expect(cards.length).toBe(view.plugins.length);
    expect(texts(section, "article.plugin > h2")).toEqual(view.plugins.map((p) => p.name));
    view.plugins.forEach((p, index) => {
      const c = cards[index];
      if (c === undefined) throw new Error(`card ${String(index)} missing`);
      expect(only(c, ":scope > p.installed", HTMLParagraphElement).textContent).toBe(`Installed: ${installedText(p)}`);
      expect(only(c, ":scope > p.latest", HTMLParagraphElement).textContent).toBe(`Latest: ${p.latest}`);
      const state = stateText(view, p);
      expect(texts(c, ":scope > p.state")).toEqual(state === null ? [] : [state]);
      expect(texts(c, ":scope > ul.copies > li")).toEqual(p.copies.map(copyText));
      expect(texts(c, ":scope > p.record-notice")).toEqual(noticeTexts(p));
      // Read-only (roadmap line 574): the release page button is the card's only control.
      expect(texts(c, "button")).toEqual(p.release_page_url === null ? [] : ["Open release page"]);
      const details = only(c, ":scope > details.notes", HTMLDetailsElement);
      expect(details.open).toBe(false);
      expect(details.firstElementChild?.tagName.toLowerCase()).toBe("summary");
      expect(texts(details, "summary")).toEqual(["Release notes"]);
      if (p.notes.length === 0) expect(texts(details, ":scope > p")).toEqual(["No release notes."]);
    });
    expect(section.querySelectorAll("a, img, iframe, object, embed, link, script, style").length).toBe(0);
    expect(attributeFindings(section)).toEqual([]);
    EXTRA[scenario]?.(section, view);
  });
}

test("does not call status commands when another instance is running", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return "Krate Manager is already running";
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(calls).toEqual(["startup_error"]);
  expect(root.querySelector("section.status")).toBeNull();
});

test("every status text is rendered by a scenario", () => {
  const rendered = GOLDENS.map(([, golden]) => renderSection(toStatusView(golden)).textContent);
  const missing: string[] = [];
  for (const [key, template] of Object.entries(STATUS_TEXTS)) {
    for (const part of template.split(/\{[a-z_]+\}/)) {
      if (part !== "" && !rendered.some((text) => text.includes(part))) missing.push(`${key}: ${JSON.stringify(part)}`);
    }
  }
  expect(missing).toEqual([]);
});

/** A promise and the functions that settle it. */
function deferred<T>(): { promise: Promise<T>; resolve: (value: T) => void; reject: (reason: Error) => void } {
  let resolve: (value: T) => void = () => undefined;
  let reject: (reason: Error) => void = () => undefined;
  const promise = new Promise<T>((yes, no) => {
    resolve = yes;
    reject = no;
  });
  return { promise, resolve, reject };
}

function settle(): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

function elsewhereCards(): { view: StatusView; holder: HTMLElement } {
  const view = toStatusView(installedElsewhere);
  const holder = document.createElement("div");
  holder.append(...renderCards(view, invoke));
  return { view, holder };
}

function withUrl(view: StatusView): PluginView {
  const p = view.plugins.find((plugin) => plugin.release_page_url !== null);
  if (p === undefined) throw new Error("installed-elsewhere has no release page url");
  return p;
}

test("renders the open release page button", () => {
  const { view, holder } = elsewhereCards();
  const urls = view.plugins.map((p) => p.release_page_url);
  expect(urls.some((u) => u === null)).toBe(true);
  expect(urls.some((u) => u !== null)).toBe(true);
  for (const p of view.plugins) {
    expect(texts(card(holder, p.name), "button.open-release")).toEqual(p.release_page_url === null ? [] : ["Open release page"]);
  }
});

test("open release page calls open_release_page once", async () => {
  const { view, holder } = elsewhereCards();
  const p = withUrl(view);
  const calls: [string, unknown][] = [];
  const pending = deferred<null>();
  mockIPC((cmd, payload) => {
    calls.push([cmd, payload]);
    if (cmd === "open_release_page") return pending.promise;
    throw new Error(`unexpected command ${cmd}`);
  });
  const button = only(card(holder, p.name), "button.open-release", HTMLButtonElement);
  button.click();
  await settle();
  expect(button.disabled).toBe(true);
  button.click();
  await settle();
  expect(calls).toEqual([["open_release_page", { url: p.release_page_url }]]);
  pending.resolve(null);
  await settle();
  expect(button.disabled).toBe(false);
  expect(card(holder, p.name).querySelector("p.open-error")).toBeNull();
});

test("open release page shows a rejection", async () => {
  const { view, holder } = elsewhereCards();
  const p = withUrl(view);
  let fail = true;
  mockIPC((cmd) => {
    if (cmd !== "open_release_page") throw new Error(`unexpected command ${cmd}`);
    if (fail) throw new Error("E_OPEN_FAILED: no browser");
    return null;
  });
  const c = card(holder, p.name);
  const button = only(c, "button.open-release", HTMLButtonElement);
  button.click();
  await settle();
  expect(texts(c, "p.open-error")).toEqual(["Could not open the release page: E_OPEN_FAILED: no browser"]);
  expect(button.disabled).toBe(false);
  fail = false;
  button.click();
  await settle();
  expect(c.querySelector("p.open-error")).toBeNull();
});

// SC-034 / FR-078: the needles are built by concatenation so this file never holds one literally.
const NAVIGATION_NEEDLES = [
  "window." + "open",
  "location." + "h" + "ref",
  "location." + "assign",
  "h" + "ref",
  "<" + "a",
  "@tauri-apps/" + "plugin-opener",
];

function navigationHits(name: string, text: string): string[] {
  const hits: string[] = [];
  text.split("\n").forEach((line, index) => {
    for (const needle of NAVIGATION_NEEDLES) if (line.includes(needle)) hits.push(`${name}:${String(index + 1)}: ${needle}`);
  });
  return hits;
}

function nonTestSources(): (readonly [string, string])[] {
  const dir = import.meta.dirname;
  return readdirSync(dir)
    .filter((name) => name.endsWith(".ts") && !name.endsWith(".test.ts"))
    .map((name) => [name, readFileSync(join(dir, name), "utf8")] as const);
}

test("no navigation path but the release page button", () => {
  const sources = nonTestSources();
  expect(sources.map(([name]) => name).sort()).toEqual(["main.ts", "manager_update.ts", "notes.ts", "status.ts", "view.ts"]);
  expect(sources.flatMap(([name, text]) => navigationHits(name, text))).toEqual([]);
  const [first] = sources;
  if (first === undefined) throw new Error("no sources");
  for (const needle of NAVIGATION_NEEDLES) {
    const planted = `${first[1]}\nconst planted = "${needle}";\n`;
    expect(navigationHits(first[0], planted).some((hit) => hit.endsWith(`: ${needle}`)), needle).toBe(true);
  }
});

// SC-024 (FR-097 to FR-099, FR-124): the action buttons, the in-card confirmation, progress, outcome and refresh.

/** One plugin in the core's `PluginView` shape: a not-installed `Alpha` unless `fields` say otherwise. */
function pluginJson(fields: Readonly<Record<string, unknown>>): Record<string, unknown> {
  return {
    name: "Alpha",
    latest: "1.2.0",
    availability: "available",
    min_os: null,
    installed: "not_installed",
    installed_version: null,
    update: "not_installed",
    copies: [],
    record: null,
    record_error: null,
    record_without_files: false,
    record_version_differs: false,
    release_page_url: null,
    notes: [],
    actions: [],
    remove_hint: null,
    ...fields,
  };
}

/** A status view through the runtime validator (PD-12), one card per entry of `plugins`. */
function viewOf(os: string, plugins: readonly Readonly<Record<string, unknown>>[]): StatusView {
  return toStatusView({
    os,
    edge_max_age_secs: 600,
    running_os: null,
    feed: { source: "fresh", fetched_at_unix: 1700000000, schema_version: 2, error: null },
    plugins: plugins.map(pluginJson),
  });
}

const MANIFEST = { kind: "manifest", version: "1.0.0", files: 4 };
const NOT_INSTALLED = { actions: ["install"] };
const UPDATE_AVAILABLE = {
  installed: "version",
  installed_version: "1.0.0",
  update: "update_available",
  record: MANIFEST,
  actions: ["update", "remove"],
};
const UP_TO_DATE = {
  installed: "version",
  installed_version: "1.2.0",
  update: "up_to_date",
  record: { ...MANIFEST, version: "1.2.0" },
  actions: ["remove"],
};
const ELSEWHERE = {
  installed: "elsewhere",
  installed_version: "0.9.0",
  copies: [
    {
      format: "vst3",
      place: "vst3_global",
      vendor_folder: false,
      nested: false,
      path: "/opt/vst3/Alpha.vst3",
      managed: false,
      loads_before_managed: false,
      version: "0.9.0",
      version_source: "classes",
      unknown_reason: null,
    },
  ],
  actions: ["install"],
};

type Answer = (cmd: string, args: Record<string, unknown> | undefined) => Promise<unknown>;

/** A recording `Invoker` (status.ts `Invoker`): every call is kept in order, `answer` settles it. */
function recorder(answer: Answer): { calls: [string, Record<string, unknown> | undefined][]; invoke: Invoker } {
  const calls: [string, Record<string, unknown> | undefined][] = [];
  const invoke: Invoker = <T>(cmd: string, args?: Record<string, unknown>): Promise<T> => {
    calls.push([cmd, args]);
    return answer(cmd, args) as Promise<T>;
  };
  return { calls, invoke };
}

/** A fake `ChannelMaker`: records each `onMessage` and returns `{ id: n }` (plan §7.1 SC-024). */
function channels(): { make: ChannelMaker; made: { id: number }[]; send: (message: unknown) => void } {
  const handlers: ((message: unknown) => void)[] = [];
  const made: { id: number }[] = [];
  const make: ChannelMaker = (onMessage) => {
    handlers.push(onMessage);
    const channel = { id: made.length + 1 };
    made.push(channel);
    return channel;
  };
  const send = (message: unknown): void => {
    for (const handler of handlers) handler(message);
  };
  return { make, made, send };
}

/** A rendered section over `view` whose `plugin_status` answers `after` and whose action commands answer `action`. */
function rig(
  view: StatusView,
  action: Promise<unknown>,
  after: StatusView = view,
): {
  section: StatusSection;
  calls: [string, Record<string, unknown> | undefined][];
  ch: ReturnType<typeof channels>;
} {
  const { calls, invoke } = recorder((cmd) => {
    if (cmd === "install_plugin" || cmd === "remove_plugin") return action;
    if (cmd === "plugin_status" || cmd === "check_now") return Promise.resolve(after);
    return Promise.reject(new Error(`unexpected command ${cmd}`));
  });
  const ch = channels();
  const section = createStatusSection(invoke, ch.make);
  section.render(view);
  section.button.disabled = false;
  return { section, calls, ch };
}

function click(root: ParentNode, selector: string): void {
  only(root, selector, HTMLButtonElement).click();
}

test("renders install for not-installed linux", () => {
  const { section } = rig(viewOf("linux", [NOT_INSTALLED]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  only(c, ":scope > div.actions", HTMLDivElement);
  expect(texts(c, "div.actions > button")).toEqual(["Install"]);
  only(c, "button.action-install", HTMLButtonElement);
  expect(c.querySelector("div.confirm")).toBeNull();
});

test("renders update and remove for update-available linux", () => {
  const { section } = rig(viewOf("linux", [UPDATE_AVAILABLE]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  expect(texts(c, "div.actions > button")).toEqual(["Update", "Remove"]);
  expect(Array.from(c.querySelectorAll("div.actions > button"), (b) => b.className)).toEqual(["action-update", "action-remove"]);
  expect(c.querySelector("button.action-install")).toBeNull();
});

// Phase 11 SC-023 (FR-025): the unit case of `setActionsEnabled`; the wiring is main.test.ts's.
test("re-enables plugin actions after a failed manager update", async () => {
  const view = viewOf("linux", [UPDATE_AVAILABLE, { ...NOT_INSTALLED, name: "Beta" }]);
  const { section, calls } = rig(view, deferred<unknown>().promise);
  const flags = (): boolean[] => [
    ...Array.from(section.element.querySelectorAll<HTMLButtonElement>("div.actions > button"), (b) => b.disabled),
    section.button.disabled,
  ];
  expect(flags()).toEqual([false, false, false, false]);
  section.setActionsEnabled(false);
  expect(flags()).toEqual([true, true, true, true]);
  section.render(view);
  expect(flags()).toEqual([true, true, true, true]);
  section.button.click();
  click(card(section.element, "Beta"), "button.action-install");
  await settle();
  expect(calls).toEqual([]);
  expect(section.element.querySelector("div.confirm")).toBeNull();
  section.setActionsEnabled(true);
  expect(flags()).toEqual([false, false, false, false]);
  // A confirmation open when the self-update starts: its Confirm is disabled, and Cancel brings back disabled buttons.
  const beta = card(section.element, "Beta");
  click(beta, "button.action-install");
  section.setActionsEnabled(false);
  expect(only(beta, "button.confirm-ok", HTMLButtonElement).disabled).toBe(true);
  click(beta, "button.confirm-cancel");
  expect(flags()).toEqual([true, true, true, true]);
  section.setActionsEnabled(true);
  expect(flags()).toEqual([false, false, false, false]);
});

// Phase 10 SC-025 (FR-130 to FR-135): the Windows / macOS sentences, written here as literals so the renderer's table
// is checked, not copied. The rejection strings are FR-133's, which the core's `InstallError` `Display` produces (PD-15).
const ADMIN_LINE = {
  windows: "Windows will ask for administrator permission once.",
  macos: "macOS will ask for your administrator password once.",
} as const;
const ELEVATED_OSES = ["windows", "macos"] as const;
const REFUSED = "E_INSTALL_REFUSED: Administrator permission was not given; nothing was changed.";
const LOCKED = "E_INSTALL_LOCKED: A plug-in file is in use. Close your DAW and retry.";
const BUSY_STALE = "E_INSTALL_ELEVATION_BUSY: An installer is still running. Wait for it to finish, then retry.";
const NOT_APPLIED =
  "E_INSTALL_NOT_APPLIED: The installer finished but the change was not applied. Check the installed version and retry.";
const WAITING = "Waiting for administrator permission and the installer…";
const RECORD_PER_OS = {
  windows: { kind: "inno", version: "1.0.0", files: null },
  macos: { kind: "pkg", version: "1.0.0", files: null },
} as const;

/** `UPDATE_AVAILABLE` with the OS's installer record (Inno on Windows, pkg on macOS). */
function updateAvailableOn(os: (typeof ELEVATED_OSES)[number]): Record<string, unknown> {
  return { ...UPDATE_AVAILABLE, record: RECORD_PER_OS[os] };
}

/** The card's action buttons' `disabled` flags, in order. */
function disabledFlags(c: ParentNode): boolean[] {
  return Array.from(c.querySelectorAll("div.actions > button"), (b) => b instanceof HTMLButtonElement && b.disabled);
}

test("renders install on windows and macos", async () => {
  for (const os of ELEVATED_OSES) {
    const view = viewOf(os, [NOT_INSTALLED, { name: "Beta", ...updateAvailableOn(os) }, { name: "Gamma", actions: [] }]);
    const { section, calls } = rig(view, deferred<unknown>().promise);
    const alpha = card(section.element, "Alpha");
    only(alpha, ":scope > div.actions", HTMLDivElement);
    expect(texts(alpha, "div.actions > button"), os).toEqual(["Install"]);
    const beta = card(section.element, "Beta");
    expect(texts(beta, "div.actions > button"), os).toEqual(["Update", "Remove"]);
    expect(Array.from(beta.querySelectorAll("div.actions > button"), (b) => b.className), os).toEqual([
      "action-update",
      "action-remove",
    ]);
    // A card the core lists no action for renders no row, exactly as on Linux.
    expect(card(section.element, "Gamma").querySelector("div.actions"), os).toBeNull();
    // Every listed action opens the in-card confirmation, which carries the OS's administrator line (FR-131).
    for (const [name, action] of [
      ["Alpha", "install"],
      ["Beta", "update"],
      ["Beta", "remove"],
    ] as const) {
      const c = card(section.element, name);
      click(c, `button.action-${action}`);
      const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
      expect(texts(confirm, "button"), `${os} ${action}`).toEqual(["Confirm", "Cancel"]);
      const lines = texts(confirm, "p");
      expect(lines[lines.length - 1], `${os} ${action}`).toBe(ADMIN_LINE[os]);
      click(c, "div.confirm button.confirm-cancel");
    }
    // A confirmed install sends the same command and arguments as on Linux.
    click(alpha, "button.action-install");
    click(alpha, "div.confirm button.confirm-ok");
    await settle();
    expect(calls, os).toEqual([["install_plugin", { name: "Alpha", expectedVersion: "1.2.0", action: "install", onProgress: { id: 1 } }]]);
  }
});

test("windows confirmation mentions the administrator prompt", () => {
  const { section, calls } = rig(viewOf("windows", [NOT_INSTALLED, { name: "Beta", ...updateAvailableOn("windows") }]), deferred<unknown>().promise);
  const alpha = card(section.element, "Alpha");
  click(alpha, "button.action-install");
  expect(texts(only(alpha, ":scope > div.confirm", HTMLDivElement), "p")).toEqual([
    "Install Alpha 1.2.0?",
    "Windows will ask for administrator permission once.",
  ]);
  const beta = card(section.element, "Beta");
  click(beta, "button.action-update");
  const confirm = only(beta, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual(["Update Beta 1.0.0 to 1.2.0?", "Windows will ask for administrator permission once."]);
  expect(confirm.textContent).not.toContain("password");
  expect(calls).toEqual([]);
  // The control: a Linux card's confirmation has no administrator line.
  const linux = rig(viewOf("linux", [NOT_INSTALLED]), deferred<unknown>().promise);
  const l = card(linux.section.element, "Alpha");
  click(l, "button.action-install");
  expect(texts(only(l, ":scope > div.confirm", HTMLDivElement), "p")).toEqual(["Install Alpha 1.2.0?"]);
});

test("macos confirmation mentions the administrator password", () => {
  const { section, calls } = rig(viewOf("macos", [NOT_INSTALLED, { name: "Beta", ...updateAvailableOn("macos") }]), deferred<unknown>().promise);
  const alpha = card(section.element, "Alpha");
  click(alpha, "button.action-install");
  expect(texts(only(alpha, ":scope > div.confirm", HTMLDivElement), "p")).toEqual([
    "Install Alpha 1.2.0?",
    "macOS will ask for your administrator password once.",
  ]);
  const beta = card(section.element, "Beta");
  click(beta, "button.action-update");
  const confirm = only(beta, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual(["Update Beta 1.0.0 to 1.2.0?", "macOS will ask for your administrator password once."]);
  expect(confirm.textContent).not.toContain("Windows");
  expect(calls).toEqual([]);
});

test("install asks for confirmation naming the plugin and version", () => {
  const { section, calls, ch } = rig(viewOf("linux", [NOT_INSTALLED]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  expect(c.querySelector("div.actions")).toBeNull();
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual(["Install Alpha 1.2.0?"]);
  expect(texts(confirm, "button")).toEqual(["Confirm", "Cancel"]);
  expect(calls).toEqual([]);
  expect(ch.made).toEqual([]);
});

test("install on an elsewhere copy names the other copy and says to remove it by hand", () => {
  const { section, calls } = rig(viewOf("linux", [ELSEWHERE]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual([
    "Install Alpha 1.2.0?",
    "Another copy is installed at /opt/vst3/Alpha.vst3. Remove it by hand; the manager does not touch it.",
  ]);
  expect(texts(confirm, "p.other-copy")).toHaveLength(1);
  expect(calls).toEqual([]);
});

test("install without an elsewhere copy shows no other-copy line", () => {
  const managedCopy = {
    ...NOT_INSTALLED,
    name: "Beta",
    copies: [{ ...ELSEWHERE.copies[0], path: "/opt/vst3/Beta.vst3", managed: true }],
  };
  const { section } = rig(viewOf("linux", [NOT_INSTALLED, managedCopy]), deferred<unknown>().promise);
  for (const name of ["Alpha", "Beta"]) {
    const c = card(section.element, name);
    click(c, "button.action-install");
    const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
    expect(texts(confirm, "p"), name).toEqual([`Install ${name} 1.2.0?`]);
    expect(confirm.querySelector("p.other-copy"), name).toBeNull();
    expect(confirm.textContent, name).not.toContain("Another copy");
  }
});

test("update asks for confirmation naming both versions", () => {
  const { section, calls } = rig(viewOf("linux", [UPDATE_AVAILABLE]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-update");
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual(["Update Alpha 1.0.0 to 1.2.0?"]);
  expect(texts(confirm, "button")).toEqual(["Confirm", "Cancel"]);
  expect(calls).toEqual([]);
});

test("remove asks for confirmation first", () => {
  const { section, calls, ch } = rig(viewOf("linux", [UP_TO_DATE]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-remove");
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual([
    "Remove Alpha? The plug-in and its factory presets are removed; your own presets are kept.",
  ]);
  expect(texts(confirm, "button")).toEqual(["Confirm", "Cancel"]);
  expect(calls).toEqual([]);
  expect(ch.made).toEqual([]);
});

test("cancelling a confirmation invokes nothing", async () => {
  const { section, calls, ch } = rig(viewOf("linux", [UPDATE_AVAILABLE]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  for (const action of ["update", "remove"]) {
    click(c, `button.action-${action}`);
    click(c, "div.confirm button.confirm-cancel");
    await settle();
    expect(c.querySelector("div.confirm"), action).toBeNull();
    expect(texts(c, "div.actions > button"), action).toEqual(["Update", "Remove"]);
    for (const b of Array.from(c.querySelectorAll("div.actions > button"))) {
      expect(b instanceof HTMLButtonElement && b.disabled, action).toBe(false);
    }
  }
  expect(calls).toEqual([]);
  expect(ch.made).toEqual([]);
  expect(c.querySelector("p.progress")).toBeNull();
});

test("confirmed install sends name, version, action and a channel", async () => {
  const { section, calls, ch } = rig(viewOf("linux", [NOT_INSTALLED]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  expect(ch.made).toEqual([{ id: 1 }]);
  expect(calls).toEqual([["install_plugin", { name: "Alpha", expectedVersion: "1.2.0", action: "install", onProgress: { id: 1 } }]]);
  expect(calls[0]?.[1]?.["onProgress"]).toBe(ch.made[0]);
  expect(Object.keys(calls[0]?.[1] ?? {}).sort()).toEqual(["action", "expectedVersion", "name", "onProgress"]);
});

test("disables the card's buttons while an action runs", async () => {
  const pending = deferred<unknown>();
  const { section, calls } = rig(viewOf("linux", [UPDATE_AVAILABLE, { name: "Beta", ...NOT_INSTALLED }]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-update");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  const buttons = Array.from(c.querySelectorAll("div.actions > button"), (b) => b instanceof HTMLButtonElement && b.disabled);
  expect(buttons).toEqual([true, true]);
  click(c, "button.action-remove");
  click(c, "button.action-update");
  await settle();
  expect(c.querySelector("div.confirm")).toBeNull();
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin"]);
  expect(only(card(section.element, "Beta"), "button.action-install", HTMLButtonElement).disabled).toBe(false);
  pending.resolve({ name: "Alpha", action: "update", version: "1.2.0", kept: [], notice: "rescan_daw" });
  await settle();
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin", "plugin_status"]);
});

test("shows placing progress", async () => {
  const pending = deferred<unknown>();
  const { section, ch } = rig(viewOf("linux", [NOT_INSTALLED]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  ch.send({ kind: "download", event: { kind: "started", total: 100, resumed_from: 0 } });
  ch.send({ kind: "download", event: { kind: "bytes", received: 40, on_disk: 40, total: 100 } });
  expect(texts(c, "p.progress")).toEqual(["Downloading 40 of 100 bytes"]);
  ch.send({ kind: "download", event: { kind: "verified" } });
  ch.send({ kind: "staging" });
  ch.send({ kind: "placing", done: 2, total: 5 });
  expect(texts(c, "p.progress")).toEqual(["Placing 2 of 5 files"]);
  ch.send({ kind: "placing", done: 5, total: 5 });
  expect(texts(c, "p.progress")).toEqual(["Placing 5 of 5 files"]);
  ch.send({ kind: "committing" });
  expect(texts(c, "p.progress")).toEqual(["Finishing"]);
  ch.send({ kind: "done" });
  expect(texts(c, "p.progress")).toEqual(["Finishing"]);
  pending.resolve({ name: "Alpha", action: "install", version: "1.2.0", kept: [], notice: "rescan_daw" });
  await settle();
  expect(section.element.querySelector("p.progress")).toBeNull();
});

test("shows the outcome and the DAW notice after an install", async () => {
  const pending = deferred<unknown>();
  const { section } = rig(viewOf("linux", [NOT_INSTALLED]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  expect(texts(c, "p.outcome")).toEqual([]);
  pending.resolve({ name: "Alpha", action: "install", version: "1.2.0", kept: [], notice: "rescan_daw" });
  await settle();
  expect(texts(card(section.element, "Alpha"), "p.outcome")).toEqual([
    "Installed 1.2.0",
    "Restart your DAW or rescan its plug-ins to load the new version.",
  ]);
});

test("shows the outcome and the DAW notice after an update", async () => {
  const pending = deferred<unknown>();
  const { section, ch } = rig(viewOf("linux", [UPDATE_AVAILABLE]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-update");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  ch.send({ kind: "placing", done: 1, total: 4 });
  pending.resolve({ name: "Alpha", action: "update", version: "1.2.0", kept: [], notice: "rescan_daw" });
  await settle();
  expect(texts(card(section.element, "Alpha"), "p.outcome")).toEqual([
    "Updated to 1.2.0",
    "Restart your DAW or rescan its plug-ins to load the new version.",
  ]);
});

test("shows kept files after remove", async () => {
  const pending = deferred<unknown>();
  const { section, calls, ch } = rig(viewOf("linux", [UP_TO_DATE]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-remove");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  expect(calls).toEqual([["remove_plugin", { name: "Alpha", onProgress: { id: 1 } }]]);
  expect(calls[0]?.[1]?.["onProgress"]).toBe(ch.made[0]);
  ch.send({ kind: "removing", done: 3, total: 7 });
  expect(texts(c, "p.progress")).toEqual(["Removing 3 of 7 files"]);
  pending.resolve({
    name: "Alpha",
    action: "remove",
    version: null,
    kept: [
      { root: "presets", path: "Alpha/Mine.vstpreset" },
      { root: "plugin", path: "Alpha.vst3/Contents/notes.txt" },
    ],
    notice: null,
  });
  await settle();
  expect(texts(card(section.element, "Alpha"), "p.outcome")).toEqual([
    "Removed",
    "Kept Alpha/Mine.vstpreset: changed since install",
    "Kept Alpha.vst3/Contents/notes.txt: changed since install",
  ]);
});

test("shows the error code on failure", async () => {
  const pending = deferred<unknown>();
  const { section, calls } = rig(viewOf("linux", [NOT_INSTALLED]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-install");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  pending.reject(new Error("E_INSTALL_STATE_CHANGED: shown not_installed, now version"));
  await settle();
  const after = card(section.element, "Alpha");
  expect(texts(after, "p.outcome")).toEqual(["E_INSTALL_STATE_CHANGED: shown not_installed, now version"]);
  expect(texts(after, "p.outcome")[0]?.startsWith("E_INSTALL_")).toBe(true);
  expect(after.querySelector("p.progress")).toBeNull();
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin", "plugin_status"]);
  expect(only(after, "button.action-install", HTMLButtonElement).disabled).toBe(false);
});

test("refreshes the status after an action and keeps the outcome", async () => {
  const pending = deferred<unknown>();
  const installed = viewOf("linux", [UP_TO_DATE, { name: "Beta", ...NOT_INSTALLED }]);
  const { section, calls } = rig(viewOf("linux", [NOT_INSTALLED, { name: "Beta", ...NOT_INSTALLED }]), pending.promise, installed);
  click(card(section.element, "Alpha"), "button.action-install");
  click(card(section.element, "Alpha"), "div.confirm button.confirm-ok");
  await settle();
  pending.resolve({ name: "Alpha", action: "install", version: "1.2.0", kept: [], notice: "rescan_daw" });
  await settle();
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin", "plugin_status"]);
  const outcome = ["Installed 1.2.0", "Restart your DAW or rescan its plug-ins to load the new version."];
  const alpha = card(section.element, "Alpha");
  expect(only(alpha, ":scope > p.installed", HTMLParagraphElement).textContent).toBe("Installed: 1.2.0");
  expect(texts(alpha, "div.actions > button")).toEqual(["Remove"]);
  expect(texts(alpha, "p.outcome")).toEqual(outcome);
  expect(texts(card(section.element, "Beta"), "p.outcome")).toEqual([]);
  // Another render (a later status) keeps the lines; Check now clears them.
  section.render(installed);
  expect(texts(card(section.element, "Alpha"), "p.outcome")).toEqual(outcome);
  section.button.click();
  await settle();
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin", "plugin_status", "check_now"]);
  expect(texts(card(section.element, "Alpha"), "p.outcome")).toEqual([]);
});

/** Clicks the card's `action` button, then the confirmation's `Confirm`. */
function confirmAction(c: ParentNode, action: string): void {
  click(c, `button.action-${action}`);
  click(c, "div.confirm button.confirm-ok");
}

test("shows waiting for administrator permission", async () => {
  for (const os of ELEVATED_OSES) {
    const pending = deferred<unknown>();
    const { section, ch } = rig(viewOf(os, [NOT_INSTALLED]), pending.promise);
    const c = card(section.element, "Alpha");
    confirmAction(c, "install");
    await settle();
    ch.send({ kind: "download", event: { kind: "bytes", received: 40, on_disk: 40, total: 100 } });
    expect(texts(c, "p.progress"), os).toEqual(["Downloading 40 of 100 bytes"]);
    ch.send({ kind: "download", event: { kind: "verified" } });
    ch.send({ kind: "elevating" });
    expect(texts(c, "p.progress"), os).toEqual([WAITING]);
    expect(disabledFlags(c), os).toEqual([true]);
    pending.resolve({ name: "Alpha", action: "install", version: "1.2.0", kept: [], notice: "rescan_daw" });
    await settle();
    const after = card(section.element, "Alpha");
    expect(section.element.querySelector("p.progress"), os).toBeNull();
    expect(texts(after, "p.outcome")[0], os).toBe("Installed 1.2.0");
  }
});

test("refused permission shows nothing changed and keeps the actions", async () => {
  for (const os of ELEVATED_OSES) {
    const pending = deferred<unknown>();
    const { section, calls, ch } = rig(viewOf(os, [updateAvailableOn(os)]), pending.promise);
    const before = card(section.element, "Alpha");
    expect(texts(before, "div.actions > button"), os).toEqual(["Update", "Remove"]);
    confirmAction(before, "update");
    await settle();
    ch.send({ kind: "elevating" });
    expect(texts(before, "p.progress"), os).toEqual([WAITING]);
    pending.reject(new Error(REFUSED));
    await settle();
    const after = card(section.element, "Alpha");
    expect(texts(after, "p.outcome"), os).toEqual([REFUSED]);
    expect(after.querySelector("p.progress"), os).toBeNull();
    expect(texts(after, "div.actions > button"), os).toEqual(["Update", "Remove"]);
    expect(Array.from(after.querySelectorAll("div.actions > button"), (b) => b.className), os).toEqual([
      "action-update",
      "action-remove",
    ]);
    expect(disabledFlags(after), os).toEqual([false, false]);
    expect(calls.map(([cmd]) => cmd), os).toEqual(["install_plugin", "plugin_status"]);
  }
});

test("locked shows close your daw and retry", async () => {
  const pending = deferred<unknown>();
  const { section, ch } = rig(viewOf("windows", [updateAvailableOn("windows")]), pending.promise);
  confirmAction(card(section.element, "Alpha"), "update");
  await settle();
  ch.send({ kind: "elevating" });
  expect(texts(card(section.element, "Alpha"), "p.progress")).toEqual([WAITING]);
  pending.reject(new Error(LOCKED));
  await settle();
  const after = card(section.element, "Alpha");
  const lines = texts(after, "p.outcome");
  expect(lines).toEqual([LOCKED]);
  expect(lines[0]?.startsWith("E_INSTALL_LOCKED")).toBe(true);
  expect(lines[0]).toContain("Close your DAW and retry.");
  expect(after.querySelector("p.progress")).toBeNull();
  expect(disabledFlags(after)).toEqual([false, false]);
});

test("macos install shows the au rescan steps", async () => {
  const pending = deferred<unknown>();
  const { section } = rig(viewOf("macos", [NOT_INSTALLED]), pending.promise);
  confirmAction(card(section.element, "Alpha"), "install");
  await settle();
  pending.resolve({ name: "Alpha", action: "install", version: "1.2.0", kept: [], notice: "rescan_daw_macos" });
  await settle();
  const after = card(section.element, "Alpha");
  const lines = texts(after, "p.outcome");
  // TA-12: the AU-cache phrase is built, never written whole in this file.
  const auCache = ["~", "Library", "Caches", "AudioUnitCache"].join("/");
  expect(lines).toEqual([
    "Installed 1.2.0",
    "Restart your DAW or rescan its plug-ins to load the new version.",
    "If a Logic or GarageBand AU does not appear: 1. Restart the Mac.",
    "2. In Logic's Plug-in Manager, use 'Reset & Rescan Selection'.",
    "3. Still missing: use 'Full Audio Unit Reset', then reopen Logic.",
    `4. Last resort: move the files in ${auCache} out, restart, and reopen Logic (the rescan can take a while).`,
    "Apple's guide: support.apple.com/en-us/122179",
  ]);
  // Apple's order (FR-133): each phrase after the one before.
  const joined = lines.join("\n");
  let from = 0;
  for (const phrase of ["Restart the Mac", "Reset & Rescan Selection", "Full Audio Unit Reset", auCache, "support.apple.com/en-us/122179"]) {
    const at = joined.indexOf(phrase, from);
    expect(at, phrase).toBeGreaterThanOrEqual(from);
    from = at + phrase.length;
  }
  // The guide is plain text: no link, no attribute beyond the FR-075 set.
  expect(section.element.querySelectorAll("a").length).toBe(0);
  expect(attributeFindings(section.element)).toEqual([]);
});

test("outside-root install shows the windows settings hint and no remove", async () => {
  const hint = "Installed outside the standard folder: uninstall it from Windows Settings → Apps.";
  const outside = { ...updateAvailableOn("windows"), actions: ["update"], remove_hint: "windows_settings" };
  const view = viewOf("windows", [
    outside,
    { ...outside, name: "Beta", actions: ["update", "remove"] },
    { name: "Gamma", ...updateAvailableOn("windows") },
  ]);
  const { section, calls } = rig(view, deferred<unknown>().promise);
  for (const name of ["Alpha", "Beta"]) {
    const c = card(section.element, name);
    expect(texts(c, ":scope > p.remove-hint"), name).toEqual([hint]);
    expect(texts(c, "div.actions > button"), name).toEqual(["Update"]);
    expect(c.querySelector("button.action-remove"), name).toBeNull();
  }
  // The control: the same card without the hint keeps Remove and shows no hint line.
  const gamma = card(section.element, "Gamma");
  expect(gamma.querySelector("p.remove-hint")).toBeNull();
  expect(texts(gamma, "div.actions > button")).toEqual(["Update", "Remove"]);
  // Update still runs the installer (spec edge case, FR-080).
  confirmAction(card(section.element, "Alpha"), "update");
  await settle();
  expect(calls.map(([cmd, args]) => [cmd, args?.["action"]])).toEqual([["install_plugin", "update"]]);
});

test("windows remove confirmation names the uninstaller", async () => {
  const { section, calls } = rig(viewOf("windows", [{ ...UP_TO_DATE, record: RECORD_PER_OS.windows }]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-remove");
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual([
    "Remove Alpha? Its installer's uninstaller removes the plug-in and its factory presets; your own presets are kept.",
    "Windows will ask for administrator permission once.",
  ]);
  expect(confirm.textContent).not.toContain("are removed;");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  expect(calls).toEqual([["remove_plugin", { name: "Alpha", onProgress: { id: 1 } }]]);
});

test("stale busy shows installer still running", async () => {
  const pending = deferred<unknown>();
  const { section, calls } = rig(viewOf("windows", [updateAvailableOn("windows")]), pending.promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-update");
  expect(texts(only(c, ":scope > div.confirm", HTMLDivElement), "p")).toEqual([
    "Update Alpha 1.0.0 to 1.2.0?",
    "Windows will ask for administrator permission once.",
  ]);
  click(c, "div.confirm button.confirm-ok");
  await settle();
  pending.reject(new Error(BUSY_STALE));
  await settle();
  const after = card(section.element, "Alpha");
  expect(texts(after, "p.outcome")).toEqual([BUSY_STALE]);
  expect(texts(after, "p.outcome")[0]).toContain("An installer is still running.");
  expect(texts(after, "div.actions > button")).toEqual(["Update", "Remove"]);
  expect(disabledFlags(after)).toEqual([false, false]);
  expect(calls.map(([cmd]) => cmd)).toEqual(["install_plugin", "plugin_status"]);
});

test("not applied shows the change was not applied", async () => {
  const pending = deferred<unknown>();
  const { section, ch } = rig(viewOf("macos", [updateAvailableOn("macos")]), pending.promise);
  confirmAction(card(section.element, "Alpha"), "update");
  await settle();
  ch.send({ kind: "elevating" });
  expect(texts(card(section.element, "Alpha"), "p.progress")).toEqual([WAITING]);
  pending.reject(new Error(NOT_APPLIED));
  await settle();
  const after = card(section.element, "Alpha");
  expect(texts(after, "p.outcome")).toEqual([NOT_APPLIED]);
  expect(texts(after, "p.outcome")[0]?.startsWith("E_INSTALL_NOT_APPLIED: ")).toBe(true);
  expect(after.querySelector("p.progress")).toBeNull();
  expect(disabledFlags(after)).toEqual([false, false]);
});

test("macos remove confirmation names the uninstaller", async () => {
  const { section, calls } = rig(viewOf("macos", [{ ...UP_TO_DATE, record: RECORD_PER_OS.macos }]), deferred<unknown>().promise);
  const c = card(section.element, "Alpha");
  click(c, "button.action-remove");
  const confirm = only(c, ":scope > div.confirm", HTMLDivElement);
  expect(texts(confirm, "p")).toEqual([
    "Remove Alpha? Its installer's uninstaller removes the plug-in and its factory presets; your own presets are kept.",
    "macOS will ask for your administrator password once.",
  ]);
  expect(confirm.textContent).not.toContain("are removed;");
  click(c, "div.confirm button.confirm-ok");
  await settle();
  expect(calls).toEqual([["remove_plugin", { name: "Alpha", onProgress: { id: 1 } }]]);
});
