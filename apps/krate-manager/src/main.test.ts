import { afterEach, expect, test } from "vitest";
import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";
import freshAllStates from "../tests/fixtures/status/fresh-all-states.json";
import installedElsewhere from "../tests/fixtures/status/installed-elsewhere.json";
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
});

/** Mocks an unlocked start whose first `plugin_status` answers `first` and whose `check_now` calls `checkNow`. */
function unlocked(first: () => unknown, checkNow: () => unknown = () => null): string[] {
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    if (cmd === "plugin_status") return first();
    if (cmd === "check_now") return checkNow();
    throw new Error(`unexpected command ${cmd}`);
  });
  return calls;
}

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
});

test("check now calls check_now and re-renders", async () => {
  const root = document.createElement("main");
  const fresh = toStatusView(freshAllStates);
  const calls = unlocked(() => toStatusView(noFeed), () => fresh);
  await renderShell(root);
  expect(cardNames(root)).toEqual(toStatusView(noFeed).plugins.map((p) => p.name));
  checkButton(root).click();
  await settle();
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status", "check_now"]);
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
  unlocked(
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
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
  expect(calls).toEqual(["startup_error", "app_version", "plugin_status"]);
});
