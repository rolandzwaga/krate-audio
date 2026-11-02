// Phase 11 SC-003 (FR-024, PD-31): the Krate Manager update line, its confirmation, progress and failure lines. The
// texts are written here as literals so the module's table is checked, not copied.
import { afterEach, expect, test, vi } from "vitest";
import { createUpdateLine, toManagerUpdateView } from "./manager_update";
import type { ChannelMaker, Invoker } from "./status";

afterEach(() => {
  vi.restoreAllMocks();
});

const AVAILABLE_LINE = "Krate Manager 0.2.0 is available.";
const CONFIRM_LINE =
  "Download Krate Manager 0.2.0, verify its signature, then close and restart. Plugin installs must be finished first.";
const MAC_LINE = "macOS may ask for your administrator password.";
const DOWNLOADING = "Downloading and verifying…";
const INSTALLING = "Installing… Krate Manager will restart.";
const SIGNATURE_SENTENCE = "The Krate Manager update failed its signature check and was not installed.";
const RELEASE_TOKEN = "release-page-0.2.0";

/** The core's `AvailableView`: an install-and-restart update to 0.2.0 whose notes hold markup as plain text. */
function availableJson(fields: Readonly<Record<string, unknown>> = {}): Record<string, unknown> {
  return {
    version: "0.2.0",
    date: "2026-11-01",
    notes: [{ type: "paragraph", inlines: [{ type: "text", text: "Faster <b>scans</b>." }] }],
    action: { kind: "install_and_restart" },
    ...fields,
  };
}

/** The core's `ManagerUpdateView` on Windows with nothing to offer, unless `fields` say otherwise. */
function viewJson(fields: Readonly<Record<string, unknown>> = {}): Record<string, unknown> {
  return { os: "windows", current: "0.1.0", available: null, error: null, ...fields };
}

type Answer = (cmd: string, args: Record<string, unknown> | undefined) => Promise<unknown>;

/** A recording `Invoker`: every call is kept in order, `answer` settles it. */
function recorder(answer: Answer): { calls: [string, Record<string, unknown> | undefined][]; invoke: Invoker } {
  const calls: [string, Record<string, unknown> | undefined][] = [];
  const invoke: Invoker = <T>(cmd: string, args?: Record<string, unknown>): Promise<T> => {
    calls.push([cmd, args]);
    return answer(cmd, args) as Promise<T>;
  };
  return { calls, invoke };
}

/** A fake `ChannelMaker`: records each `onMessage` and returns `{ id: n }`. */
function channels(): { make: ChannelMaker; send: (message: unknown) => void } {
  const handlers: ((message: unknown) => void)[] = [];
  const make: ChannelMaker = (onMessage) => {
    handlers.push(onMessage);
    return { id: handlers.length };
  };
  const send = (message: unknown): void => {
    for (const handler of handlers) handler(message);
  };
  return { make, send };
}

/** A promise and the functions that settle it. */
function deferred<T>(): { promise: Promise<T>; resolve: (value: T) => void; reject: (reason: unknown) => void } {
  let resolve: (value: T) => void = () => undefined;
  let reject: (reason: unknown) => void = () => undefined;
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

function texts(root: ParentNode, selector: string): string[] {
  return Array.from(root.querySelectorAll(selector), (e) => e.textContent);
}

function only<T extends Element>(root: ParentNode, selector: string, kind: new () => T): T {
  const found = root.querySelectorAll(selector);
  expect(found.length, selector).toBe(1);
  const [first] = found;
  if (!(first instanceof kind)) throw new Error(`${selector} is not a ${kind.name}`);
  return first;
}

/** A checked line over `view` whose install answers `install` and whose release-page open answers `open`. */
async function rig(
  view: unknown,
  install: Promise<unknown> = deferred<unknown>().promise,
  open: () => Promise<unknown> = () => Promise.resolve(null),
): Promise<{
  element: HTMLElement;
  calls: [string, Record<string, unknown> | undefined][];
  enabled: boolean[];
  ch: ReturnType<typeof channels>;
}> {
  const { calls, invoke } = recorder((cmd) => {
    if (cmd === "manager_update_check") return Promise.resolve(view);
    if (cmd === "manager_update_install") return install;
    if (cmd === "open_release_page") return open();
    return Promise.reject(new Error(`unexpected command ${cmd}`));
  });
  const enabled: boolean[] = [];
  const ch = channels();
  const line = createUpdateLine(
    invoke,
    {
      setEnabled(value) {
        enabled.push(value);
      },
    },
    ch.make,
  );
  await line.check();
  return { element: line.element, calls, enabled, ch };
}

function confirmUpdate(element: HTMLElement): void {
  only(element, "button.update-restart", HTMLButtonElement).click();
  only(element, "button.update-yes", HTMLButtonElement).click();
}

/** The serialized `ErrorView` `manager_update_install` rejects with (tasks TA-21). */
const SIGNATURE_ERROR = JSON.stringify({ code: "E_SELF_UPDATE_SIGNATURE", sentence: SIGNATURE_SENTENCE, detail: "bad sig" });

test("renders the manager update line", async () => {
  const { element, calls } = await rig(viewJson({ available: availableJson() }));
  expect(calls).toEqual([["manager_update_check", undefined]]);
  expect(texts(element, "p.update-available")).toEqual([AVAILABLE_LINE]);
  const notes = only(element, "div.update-notes", HTMLDivElement);
  expect(texts(notes, "p")).toEqual(["Faster <b>scans</b>."]);
  expect(notes.querySelector("b")).toBeNull();
  expect(texts(element, "button")).toEqual(["Update and restart"]);
  expect(element.querySelector("div.update-confirm")).toBeNull();
  // Nothing announced, nothing failed: the line is empty.
  const none = await rig(viewJson());
  expect(none.element.childNodes.length).toBe(0);
  // A view the strict parser refuses shows nothing, like any parser failure.
  for (const bad of [
    { ...viewJson(), extra: 1 },
    viewJson({ os: "beos" }),
    viewJson({ available: { ...availableJson(), extra: 1 } }),
    viewJson({ available: availableJson({ notes: [{ type: "html", text: "<b>x</b>" }] }) }),
  ]) {
    expect(() => toManagerUpdateView(bad)).toThrow(TypeError);
    expect((await rig(bad)).element.childNodes.length).toBe(0);
  }
});

test("confirms before updating", async () => {
  const confirm = vi.spyOn(window, "confirm");
  const { element, calls, enabled } = await rig(viewJson({ available: availableJson() }));
  only(element, "button.update-restart", HTMLButtonElement).click();
  const dialog = only(element, "div.update-confirm", HTMLDivElement);
  expect(texts(dialog, "p")).toEqual([CONFIRM_LINE]);
  expect(texts(dialog, "button")).toEqual(["Confirm", "Cancel"]);
  expect(element.querySelector("button.update-restart")).toBeNull();
  await settle();
  expect(calls.map(([cmd]) => cmd)).toEqual(["manager_update_check"]);
  only(dialog, "button.update-no", HTMLButtonElement).click();
  await settle();
  expect(element.querySelector("div.update-confirm")).toBeNull();
  expect(texts(element, "button")).toEqual(["Update and restart"]);
  expect(calls.map(([cmd]) => cmd)).toEqual(["manager_update_check"]);
  expect(enabled).toEqual([]);
  confirmUpdate(element);
  await settle();
  expect(calls).toEqual([
    ["manager_update_check", undefined],
    ["manager_update_install", { onProgress: { id: 1 } }],
  ]);
  expect(enabled).toEqual([false]);
  expect(element.querySelector("div.update-confirm")).toBeNull();
  expect(only(element, "button.update-restart", HTMLButtonElement).disabled).toBe(true);
  expect(texts(element, "p.update-progress")).toEqual([DOWNLOADING]);
  expect(confirm).not.toHaveBeenCalled();
});

test("manager update confirmation names the admin prompt on macOS only", async () => {
  for (const os of ["macos", "windows", "linux"]) {
    const { element } = await rig(viewJson({ os, available: availableJson() }));
    only(element, "button.update-restart", HTMLButtonElement).click();
    const dialog = only(element, "div.update-confirm", HTMLDivElement);
    expect(texts(dialog, "p"), os).toEqual(os === "macos" ? [CONFIRM_LINE, MAC_LINE] : [CONFIRM_LINE]);
    expect(element.textContent.includes("administrator"), os).toBe(os === "macos");
  }
});

test("shows a failed update by code", async () => {
  for (const [rejection, line] of [
    [SIGNATURE_ERROR, `E_SELF_UPDATE_SIGNATURE: ${SIGNATURE_SENTENCE}`],
    [new Error(SIGNATURE_ERROR), `E_SELF_UPDATE_SIGNATURE: ${SIGNATURE_SENTENCE}`],
    ["E_SELF_UPDATE_INSTALL: not JSON", "E_SELF_UPDATE_INSTALL: not JSON"],
  ] as const) {
    const install = deferred<unknown>();
    const { element, enabled } = await rig(viewJson({ available: availableJson() }), install.promise);
    confirmUpdate(element);
    await settle();
    install.reject(rejection);
    await settle();
    expect(texts(element, "p.update-failure")).toEqual([line]);
    expect(element.querySelector("p.update-failure")?.textContent.startsWith("E_SELF_UPDATE_")).toBe(true);
    expect(element.querySelector("p.update-progress")).toBeNull();
    expect(enabled).toEqual([false, true]);
    expect(only(element, "button.update-restart", HTMLButtonElement).disabled).toBe(false);
  }
});

test("shows no banner when the check fails", async () => {
  const error = { code: "E_SELF_UPDATE_NETWORK", sentence: "Could not reach the Krate Manager update server.", detail: "x" };
  const { element } = await rig(viewJson({ error }));
  expect(texts(element, "p")).toEqual(["Could not check for a Krate Manager update (E_SELF_UPDATE_NETWORK)."]);
  expect(element.querySelector("p.update-muted")).not.toBeNull();
  expect(element.querySelector("p.update-available")).toBeNull();
  expect(element.querySelector("button")).toBeNull();
  // A URL refusal carries its reason; an unknown reason or key fails the parser.
  const url = { code: "E_SELF_UPDATE_URL", sentence: "s", reason: "host" };
  expect(texts((await rig(viewJson({ error: url }))).element, "p.update-muted")).toEqual([
    "Could not check for a Krate Manager update (E_SELF_UPDATE_URL).",
  ]);
  expect(() => toManagerUpdateView(viewJson({ error: { ...url, reason: "dns" } }))).toThrow(TypeError);
  expect(() => toManagerUpdateView(viewJson({ error: { ...url, extra: 1 } }))).toThrow(TypeError);
  // A rejected check shows nothing.
  const { invoke } = recorder(() => Promise.reject(new Error("E_SELF_UPDATE_NETWORK: x")));
  const line = createUpdateLine(invoke, { setEnabled: () => undefined }, channels().make);
  await line.check();
  expect(line.element.childNodes.length).toBe(0);
});

test("shows downloading then installing progress", async () => {
  const install = deferred<unknown>();
  const { element, enabled, ch } = await rig(viewJson({ available: availableJson() }), install.promise);
  const seen: string[] = [];
  const record = (): void => {
    const text = element.querySelector("p.update-progress")?.textContent;
    if (text !== undefined && seen[seen.length - 1] !== text) seen.push(text);
  };
  confirmUpdate(element);
  await settle();
  record();
  for (const bad of [{ event: "verifying" }, { event: "installing", extra: 1 }, "installing", null]) {
    ch.send(bad);
    expect(texts(element, "p.update-progress")).toEqual([DOWNLOADING]);
  }
  ch.send({ event: "downloading" });
  record();
  ch.send({ event: "installing" });
  record();
  ch.send({ event: "downloading" });
  record();
  expect(seen).toEqual([DOWNLOADING, INSTALLING]);
  expect(texts(element, "p.update-progress")).toEqual([INSTALLING]);
  install.reject(SIGNATURE_ERROR);
  await settle();
  expect(texts(element, "p.update-failure")).toEqual([`E_SELF_UPDATE_SIGNATURE: ${SIGNATURE_SENTENCE}`]);
  expect(element.querySelector("p.update-progress")).toBeNull();
  expect(enabled).toEqual([false, true]);
});

test("offers the new .deb instead of an update on a system-package install", async () => {
  const action = { kind: "open_release_page", url: RELEASE_TOKEN };
  const { element, calls, enabled } = await rig(viewJson({ os: "linux", available: availableJson({ action }) }));
  expect(texts(element, "p.update-available")).toEqual([AVAILABLE_LINE]);
  expect(texts(element, "button")).toEqual(["Download the new .deb"]);
  expect(element.textContent.includes("Update and restart")).toBe(false);
  only(element, "button", HTMLButtonElement).click();
  await settle();
  expect(calls).toEqual([
    ["manager_update_check", undefined],
    ["open_release_page", { url: RELEASE_TOKEN }],
  ]);
  expect(element.querySelector("div.update-confirm")).toBeNull();
  expect(element.querySelector("p.update-progress")).toBeNull();
  expect(enabled).toEqual([]);
  // Exactly the two action shapes pass the parser.
  for (const bad of [
    { kind: "open_release_page" },
    { kind: "open_release_page", url: 3 },
    { kind: "open_release_page", url: RELEASE_TOKEN, extra: 1 },
    { kind: "install_and_restart", url: RELEASE_TOKEN },
    { kind: "download_and_install" },
    "install_and_restart",
  ]) {
    const view = viewJson({ os: "linux", available: availableJson({ action: bad }) });
    expect(() => toManagerUpdateView(view)).toThrow(TypeError);
    expect((await rig(view)).element.childNodes.length).toBe(0);
  }
});

test("a failed .deb release-page open shows its code", async () => {
  const action = { kind: "open_release_page", url: RELEASE_TOKEN };
  const view = viewJson({ os: "linux", available: availableJson({ action }) });
  const { element, enabled } = await rig(view, undefined, () => Promise.reject(new Error("E_OPEN_REJECTED")));
  const button = only(element, "button", HTMLButtonElement);
  button.click();
  await settle();
  const error = button.nextElementSibling;
  expect(error?.textContent).toBe("Could not open the release page: E_OPEN_REJECTED");
  expect(texts(element, "p.open-error")).toEqual(["Could not open the release page: E_OPEN_REJECTED"]);
  expect(button.disabled).toBe(false);
  expect(enabled).toEqual([]);
});
