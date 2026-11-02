import { afterEach, expect, test } from "vitest";
import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";
import cachedStale from "../tests/fixtures/status/cached-stale.json";
import freshAllStates from "../tests/fixtures/status/fresh-all-states.json";
import installedElsewhere from "../tests/fixtures/status/installed-elsewhere.json";
import macosAuCopies from "../tests/fixtures/status/macos-au-copies.json";
import noFeed from "../tests/fixtures/status/no-feed.json";
import { renderShell } from "./main";
import { toStatusView } from "./view";

afterEach(clearMocks);

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

function checkButton(root: HTMLElement): HTMLButtonElement {
  const button = root.querySelector("section.status button.check-now");
  if (!(button instanceof HTMLButtonElement)) throw new Error("no Check now button");
  return button;
}

function feedLine(root: HTMLElement): string | undefined {
  return root.querySelector("section.status p.feed-line")?.textContent;
}

function cardNames(root: HTMLElement): string[] {
  return Array.from(root.querySelectorAll("section.status article.plugin > h2"), (h) => h.textContent);
}

test("shows the title and the version", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return toStatusView(noFeed);
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  expect(root.querySelector("p.version")?.textContent).toBe("0.1.0");
  expect(root.querySelector(".notice")).toBeNull();
  expect(root.querySelector("section.status")).not.toBeNull();
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status", "manager_update_check"]);
});

test('shows "version unavailable" when app_version fails', async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    throw new Error("x");
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  expect(root.querySelector("p.version")?.textContent).toBe("version unavailable");
  expect(root.querySelector(".notice")).toBeNull();
  expect(feedLine(root)).toBe("Could not load the plugin list: x");
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status", "manager_update_check"]);
});

test("shows only the notice when another instance is running", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return "Krate Manager is already running";
    if (cmd === "app_version") return "0.1.0";
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  const notices = root.querySelectorAll("p.notice");
  expect(notices.length).toBe(1);
  expect(notices[0]?.textContent).toBe("Krate Manager is already running");
  expect(root.querySelector("p.version")).toBeNull();
  expect(root.querySelector("section.status")).toBeNull();
  expect(calls).toEqual(["startup_error"]);
});

test("shows the version when startup_error fails", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") throw new Error("x");
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return toStatusView(noFeed);
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  expect(root.querySelector("p.version")?.textContent).toBe("0.1.0");
  expect(root.querySelector(".notice")).toBeNull();
  expect(root.querySelector("section.status")).not.toBeNull();
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status", "manager_update_check"]);
});

/** The core's `ManagerUpdateView` with nothing to offer. */
const NO_UPDATE = { os: "windows", current: "0.1.0", available: null, error: null };

/**
 * Mocks an unlocked start whose first `plugin_status` answers `first`, whose `check_now` calls `checkNow` and whose
 * `manager_update_check` calls `managerCheck`.
 */
function unlocked(first: () => unknown, checkNow: () => unknown = () => null, managerCheck: () => unknown = () => NO_UPDATE): string[] {
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return first();
    if (cmd === "check_now") return checkNow();
    if (cmd === "manager_update_check") return managerCheck();
    throw new Error(`unexpected command ${cmd}`);
  });
  return calls;
}

const AFTER_START = ["startup_error", "app_version", "plugin_status", "manager_update_check"];

test("check now disables the button while pending", async () => {
  const root = document.createElement("main");
  const pending = deferred<unknown>();
  const view = toStatusView(installedElsewhere);
  const calls = unlocked(() => view, () => pending.promise);
  await renderShell(root);
  const button = checkButton(root);
  expect(button.disabled).toBe(false);
  button.click();
  await settle();
  expect(button.disabled).toBe(true);
  expect(button.textContent).toBe("Checking…");
  button.click();
  await settle();
  expect(calls.filter((cmd) => cmd === "check_now").length).toBe(1);
  pending.resolve(view);
  await settle();
  expect(button.disabled).toBe(false);
  expect(button.textContent).toBe("Check now");
  expect(calls).toEqual([...AFTER_START, "check_now", "manager_update_check"]);
});

test("check now calls check_now and re-renders", async () => {
  const root = document.createElement("main");
  const fresh = toStatusView(freshAllStates);
  const calls = unlocked(() => toStatusView(noFeed), () => fresh);
  await renderShell(root);
  expect(cardNames(root)).toEqual(toStatusView(noFeed).plugins.map((p) => p.name));
  checkButton(root).click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "check_now", "manager_update_check"]);
  expect(cardNames(root)).toEqual(fresh.plugins.map((p) => p.name));
  expect(cardNames(root).length).toBeGreaterThan(0);
  expect(feedLine(root)?.startsWith("Checked ")).toBe(true);
  expect(root.querySelectorAll("section.status").length).toBe(1);
  expect(checkButton(root).disabled).toBe(false);
  expect(checkButton(root).textContent).toBe("Check now");
});

test("check now recovers from a rejection", async () => {
  const root = document.createElement("main");
  const view = toStatusView(installedElsewhere);
  const calls = unlocked(
    () => view,
    () => {
      throw new Error("E_FEED_OFFLINE: offline");
    },
  );
  await renderShell(root);
  const before = cardNames(root);
  expect(before.length).toBeGreaterThan(0);
  checkButton(root).click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "check_now", "manager_update_check"]);
  expect(feedLine(root)).toBe("Could not load the plugin list: E_FEED_OFFLINE: offline");
  expect(cardNames(root)).toEqual(before);
  expect(checkButton(root).disabled).toBe(false);
  expect(checkButton(root).textContent).toBe("Check now");
});

test("shows the ten-minute note", async () => {
  const root = document.createElement("main");
  const view = toStatusView(noFeed);
  expect(view.edge_max_age_secs).toBe(600);
  unlocked(() => view);
  await renderShell(root);
  const notes = root.querySelectorAll("section.status p.check-note");
  expect(notes.length).toBe(1);
  expect(notes[0]?.textContent).toBe("A new release can take up to 10 minutes to appear.");
});

test("shows the pending text until the first result", async () => {
  const root = document.createElement("main");
  const arrived = deferred<null>();
  const pending = deferred<unknown>();
  const calls = unlocked(() => {
    arrived.resolve(null);
    return pending.promise;
  });
  const done = renderShell(root);
  await arrived.promise;
  expect(feedLine(root)).toBe("Checking for plugins…");
  expect(checkButton(root).disabled).toBe(true);
  expect(root.querySelector("section.status p.check-note")).toBeNull();
  expect(cardNames(root)).toEqual([]);
  const fresh = toStatusView(freshAllStates);
  pending.resolve(fresh);
  await done;
  expect(feedLine(root)).not.toBe("Checking for plugins…");
  expect(root.querySelector("section.status")?.textContent.includes("Checking for plugins…")).toBe(false);
  expect(cardNames(root)).toEqual(fresh.plugins.map((p) => p.name));
  expect(checkButton(root).disabled).toBe(false);
  expect(calls).toEqual(AFTER_START);
});

test("check now stays disabled until the first result", async () => {
  const root = document.createElement("main");
  const arrived = deferred<null>();
  const pending = deferred<unknown>();
  const calls = unlocked(() => {
    arrived.resolve(null);
    return pending.promise;
  });
  const done = renderShell(root);
  await arrived.promise;
  const button = checkButton(root);
  expect(button.disabled).toBe(true);
  button.click();
  await settle();
  expect(button.disabled).toBe(true);
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
  pending.resolve(toStatusView(noFeed));
  await done;
  expect(button.disabled).toBe(false);
  expect(calls).toEqual(AFTER_START);
});

test("check now is enabled after a rejected first load", async () => {
  const root = document.createElement("main");
  const calls = unlocked(() => {
    throw new Error("E_FEED_OFFLINE: offline");
  });
  await renderShell(root);
  expect(feedLine(root)).toBe("Could not load the plugin list: E_FEED_OFFLINE: offline");
  expect(cardNames(root)).toEqual([]);
  expect(root.querySelector("section.status p.check-note")).toBeNull();
  expect(checkButton(root).disabled).toBe(false);
  expect(calls).toEqual(AFTER_START);
});

// Phase 11 SC-025 (FR-024, FR-026): the manager's own update check follows the first `plugin_status` once it settles.

/** The core's `ManagerUpdateView` offering 0.2.0 as an install-and-restart update. */
const UPDATE = {
  os: "windows",
  current: "0.1.0",
  available: { version: "0.2.0", date: null, notes: [], action: { kind: "install_and_restart" } },
  error: null,
};

function updateLine(root: HTMLElement): HTMLElement {
  const line = root.querySelector("div.manager-update");
  if (!(line instanceof HTMLElement)) throw new Error("no update line");
  return line;
}

test("checks for a manager update after plugin_status is rejected", async () => {
  const root = document.createElement("main");
  const calls = unlocked(
    () => {
      throw new Error("E_STARTUP_ROOTS: no roots");
    },
    () => null,
    () => UPDATE,
  );
  await renderShell(root);
  expect(feedLine(root)).toBe("Could not load the plugin list: E_STARTUP_ROOTS: no roots");
  expect(calls).toEqual(AFTER_START);
  expect(updateLine(root).querySelector("p.update-available")?.textContent).toBe("Krate Manager 0.2.0 is available.");
  // The line sits in the header: after the version line, before the status section.
  expect(Array.from(root.children, (e) => e.tagName.toLowerCase() + "." + e.className)).toEqual([
    "h1.",
    "p.version",
    "div.manager-update",
    "section.status",
  ]);
});

test("checks for a manager update after plugin_status succeeds", async () => {
  const root = document.createElement("main");
  const arrived = deferred<null>();
  const pending = deferred<unknown>();
  const calls = unlocked(
    () => {
      arrived.resolve(null);
      return pending.promise;
    },
    () => toStatusView(noFeed),
    () => UPDATE,
  );
  const done = renderShell(root);
  await arrived.promise;
  await settle();
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
  pending.resolve(toStatusView(noFeed));
  await done;
  expect(calls).toEqual(AFTER_START);
  expect(updateLine(root).querySelector("p.update-available")?.textContent).toBe("Krate Manager 0.2.0 is available.");
  checkButton(root).click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "check_now", "manager_update_check"]);
  expect(updateLine(root).querySelectorAll("p.update-available").length).toBe(1);
});

test("does not check for a manager update when the context is locked", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return "Krate Manager is already running";
    if (cmd === "manager_update_check") return UPDATE;
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  await settle();
  expect(calls).toEqual(["startup_error"]);
  expect(root.querySelector("div.manager-update")).toBeNull();
  expect(root.textContent.includes("is available")).toBe(false);
});

test("disables plugin actions while the manager updates itself", async () => {
  const root = document.createElement("main");
  const install = deferred<unknown>();
  const plugin = {
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
    actions: ["install"],
    remove_hint: null,
  };
  const status = toStatusView({
    os: "windows",
    edge_max_age_secs: 600,
    running_os: null,
    feed: { source: "fresh", fetched_at_unix: 1700000000, schema_version: 2, error: null },
    plugins: [plugin],
    orphans: [],
  });
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return status;
    if (cmd === "manager_update_check") return UPDATE;
    if (cmd === "manager_update_install") return install.promise;
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  const flags = (): boolean[] => [
    ...Array.from(root.querySelectorAll<HTMLButtonElement>("section.status div.actions > button"), (b) => b.disabled),
    checkButton(root).disabled,
  ];
  expect(flags()).toEqual([false, false]);
  const line = updateLine(root);
  const restart = line.querySelector("button.update-restart");
  if (!(restart instanceof HTMLButtonElement)) throw new Error("no Update and restart button");
  expect(restart.textContent).toBe("Update and restart");
  restart.click();
  expect(flags()).toEqual([false, false]);
  const yes = line.querySelector("button.update-yes");
  if (!(yes instanceof HTMLButtonElement)) throw new Error("no confirmation");
  yes.click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "manager_update_install"]);
  expect(flags()).toEqual([true, true]);
  expect(line.querySelector("p.update-progress")?.textContent).toBe("Downloading and verifying…");
  const sentence = "Krate Manager could not install its update. It keeps running the current version.";
  install.reject(new Error(JSON.stringify({ code: "E_SELF_UPDATE_INSTALL", sentence, detail: "x" })));
  await settle();
  expect(flags()).toEqual([false, false]);
  expect(line.querySelector("p.update-failure")?.textContent).toBe(`E_SELF_UPDATE_INSTALL: ${sentence}`);
});

// Phase 12 PD-14 (FR-047, FR-048): the settings section mounts only after a Linux status and re-reads it on a root change.

/** The core's `SettingsView` on Linux: the default root, unlocked, unless `fields` say otherwise. */
function settingsJson(fields: Readonly<Record<string, unknown>> = {}): Record<string, unknown> {
  return {
    os: "linux",
    linux_plugin_root: "/home/alpha/.vst3",
    root_is_default: true,
    graphics_workaround: false,
    root_locked_by: [],
    error: null,
    ...fields,
  };
}

const LINUX_NO_UPDATE = { ...NO_UPDATE, os: "linux" };

/** Mocks an unlocked Linux start over `cached-stale.json`; `save` answers each `settings_save`. */
function linuxStart(save: (args: unknown) => unknown = () => settingsJson()): string[] {
  const calls: string[] = [];
  mockIPC((cmd, args) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return toStatusView(cachedStale);
    if (cmd === "check_now") return toStatusView(cachedStale);
    if (cmd === "manager_update_check") return LINUX_NO_UPDATE;
    if (cmd === "settings_view") return settingsJson();
    if (cmd === "settings_save") return save(args);
    throw new Error(`unexpected command ${cmd}`);
  });
  return calls;
}

function count(calls: readonly string[], cmd: string): number {
  return calls.filter((c) => c === cmd).length;
}

test("main_mounts_settings_on_linux", async () => {
  expect(cachedStale.os).toBe("linux");
  const root = document.createElement("main");
  const calls = linuxStart();
  await renderShell(root);
  expect(calls).toEqual([...AFTER_START, "settings_view"]);
  expect(calls.indexOf("settings_view")).toBeGreaterThan(calls.indexOf("plugin_status"));
  expect(root.querySelectorAll("section.settings").length).toBe(1);
  // Below the status section.
  expect(root.querySelector("section.status")?.nextElementSibling?.className).toBe("settings");
  expect(root.querySelector("section.settings button.settings-toggle")?.textContent).toBe("Settings");
  // Later settles never read the settings again.
  checkButton(root).click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "settings_view", "check_now", "manager_update_check"]);
  expect(count(calls, "settings_view")).toBe(1);
  expect(root.querySelectorAll("section.settings").length).toBe(1);
});

test("main_skips_settings_on_macos", async () => {
  expect(macosAuCopies.os).toBe("macos");
  const root = document.createElement("main");
  const calls = unlocked(
    () => toStatusView(macosAuCopies),
    () => toStatusView(macosAuCopies),
    () => ({ ...NO_UPDATE, os: "macos" }),
  );
  await renderShell(root);
  expect(calls).toEqual(AFTER_START);
  expect(root.querySelector("section.settings")).toBeNull();
  checkButton(root).click();
  await settle();
  expect(calls).toEqual([...AFTER_START, "check_now", "manager_update_check"]);
  expect(root.querySelector("section.settings")).toBeNull();
});

test("main_rereads_status_after_root_change", async () => {
  const root = document.createElement("main");
  let answer = settingsJson({ linux_plugin_root: "/home/alpha/plugins", root_is_default: false });
  const calls = linuxStart(() => answer);
  await renderShell(root);
  expect(count(calls, "plugin_status")).toBe(1);
  const section = root.querySelector("section.settings");
  if (!(section instanceof HTMLElement)) throw new Error("no settings section");
  section.querySelector<HTMLButtonElement>("button.settings-toggle")?.click();
  const field = section.querySelector("input.root");
  const save = section.querySelector("button.save");
  const graphics = section.querySelector("input.graphics");
  if (!(field instanceof HTMLInputElement) || !(save instanceof HTMLButtonElement) || !(graphics instanceof HTMLInputElement)) {
    throw new Error("no settings controls");
  }
  // A saved root change: exactly one further plugin_status.
  field.value = "/home/alpha/plugins";
  save.click();
  await settle();
  expect(count(calls, "settings_save")).toBe(1);
  expect(count(calls, "plugin_status")).toBe(2);
  expect(calls[calls.length - 1]).toBe("plugin_status");
  // A save that keeps the root: none.
  answer = settingsJson({ linux_plugin_root: "/home/alpha/plugins", root_is_default: false, graphics_workaround: true });
  graphics.checked = true;
  save.click();
  await settle();
  expect(count(calls, "settings_save")).toBe(2);
  expect(count(calls, "plugin_status")).toBe(2);
  expect(count(calls, "settings_view")).toBe(1);
});

/** One Windows card offering an update from `installed` to `latest`. */
function updateCard(name: string, installed: string, latest: string): Record<string, unknown> {
  return {
    name,
    latest,
    availability: "available",
    min_os: null,
    installed: "version",
    installed_version: installed,
    update: "update_available",
    copies: [],
    record: null,
    record_error: null,
    record_without_files: false,
    record_version_differs: false,
    release_page_url: null,
    notes: [],
    actions: ["update", "remove"],
    remove_hint: null,
  };
}

test("update_all_excludes_manager_line", async () => {
  const status = toStatusView({
    os: "windows",
    edge_max_age_secs: 600,
    running_os: null,
    feed: { source: "fresh", fetched_at_unix: 1700000000, schema_version: 2, error: null },
    plugins: [updateCard("Alpha", "1.0.0", "1.2.0"), updateCard("Beta", "2.0.0", "2.1.0")],
    orphans: [],
  });
  /** Renders the shell with `manager` as the manager's update check; returns the Update-all items sent, if any. */
  async function shell(manager: unknown): Promise<{ root: HTMLElement; items: unknown[] }> {
    const root = document.createElement("main");
    const items: unknown[] = [];
    mockIPC((cmd, args) => {
      if (cmd === "startup_error") return null;
      if (cmd === "app_version") return "0.1.0";
      if (cmd === "plugin_status") return status;
      if (cmd === "manager_update_check") return manager;
      if (cmd === "update_all") {
        items.push((args as Record<string, unknown> | undefined)?.["items"]);
        return new Promise(() => undefined); // stays running: the case is about what was sent
      }
      throw new Error(`unexpected command ${cmd}`);
    });
    await renderShell(root);
    return { root, items };
  }
  function control(root: HTMLElement): HTMLButtonElement {
    const found = root.querySelector("section.status button.update-all");
    if (!(found instanceof HTMLButtonElement)) throw new Error("no Update all control");
    return found;
  }

  const without = await shell(NO_UPDATE);
  expect(without.root.querySelector("p.update-available")).toBeNull();
  const enabledWithout = !control(without.root).disabled;
  clearMocks();

  const offered = await shell(UPDATE);
  expect(updateLine(offered.root).querySelector("p.update-available")?.textContent).toBe("Krate Manager 0.2.0 is available.");
  const all = control(offered.root);
  // The manager's update does not enable, disable or alter the control.
  expect(!all.disabled).toBe(enabledWithout);
  expect(all.textContent).toBe(control(without.root).textContent);
  all.click();
  const confirm = offered.root.querySelector("section.status div.confirm-all");
  if (!(confirm instanceof HTMLElement)) throw new Error("no Update all confirmation");
  const lines = Array.from(confirm.querySelectorAll("p.confirm-line"), (p) => p.textContent);
  expect(lines.length).toBe(2);
  expect(lines[0]?.includes("Alpha")).toBe(true);
  expect(lines[1]?.includes("Beta")).toBe(true);
  expect(confirm.textContent.includes("Krate Manager")).toBe(false);
  expect(confirm.textContent.includes("0.2.0")).toBe(false);
  const ok = confirm.querySelector("button.confirm-ok");
  if (!(ok instanceof HTMLButtonElement)) throw new Error("no confirm button");
  ok.click();
  await settle();
  expect(offered.items).toEqual([
    [
      { name: "Alpha", expectedVersion: "1.2.0" },
      { name: "Beta", expectedVersion: "2.1.0" },
    ],
  ]);
});
