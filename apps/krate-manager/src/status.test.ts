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
  expect(sources.map(([name]) => name).sort()).toEqual(["main.ts", "notes.ts", "status.ts", "view.ts"]);
  expect(sources.flatMap(([name, text]) => navigationHits(name, text))).toEqual([]);
  const [first] = sources;
  if (first === undefined) throw new Error("no sources");
  for (const needle of NAVIGATION_NEEDLES) {
    const planted = `${first[1]}\nconst planted = "${needle}";\n`;
    expect(navigationHits(first[0], planted).some((hit) => hit.endsWith(`: ${needle}`)), needle).toBe(true);
  }
});
