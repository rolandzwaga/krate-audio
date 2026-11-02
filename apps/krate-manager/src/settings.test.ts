// Phase 12 SC-011 (FR-047) and SC-027 (FR-048): the Linux settings section alone; its mount is proven in main.test.ts.
import { expect, test } from "vitest";
import { createSettingsSection } from "./settings";
import type { Invoker } from "./status";
import { toSettingsView } from "./view";
import type { SettingsView } from "./view";

// FR-047's texts, written here as literals so the section's table is checked, not copied.
const GRAPHICS_LABEL =
  "Work around a blank or flickering window (NVIDIA): takes effect the next time Krate Manager starts";
const DEFAULT_ROOT = "/home/alpha/.vst3";
const OTHER_ROOT = "/home/alpha/plugins";

/** A settings view through the runtime validator: the default root, unlocked, toggle off, unless `fields` say otherwise. */
function settingsOf(fields: Readonly<Record<string, unknown>> = {}): SettingsView {
  return toSettingsView({
    os: "linux",
    linux_plugin_root: DEFAULT_ROOT,
    root_is_default: true,
    graphics_workaround: false,
    root_locked_by: [],
    error: null,
    ...fields,
  });
}

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

function settle(): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

function one<T extends Element>(scope: ParentNode, selector: string, type: new () => T): T {
  const found = scope.querySelector(selector);
  if (!(found instanceof type)) throw new Error(`no ${selector}`);
  return found;
}

/** The section's controls; the toggle has been clicked once, so the body is open. */
function opened(element: HTMLElement): {
  body: HTMLDivElement;
  root: HTMLInputElement;
  save: HTMLButtonElement;
  useDefault: HTMLButtonElement;
  graphics: HTMLInputElement;
  result: HTMLParagraphElement;
} {
  const body = one(element, ":scope > div.settings-body", HTMLDivElement);
  if (body.hidden) one(element, ":scope > button.settings-toggle", HTMLButtonElement).click();
  return {
    body,
    root: one(body, "input.root", HTMLInputElement),
    save: one(body, "button.save", HTMLButtonElement),
    useDefault: one(body, "button.use-default", HTMLButtonElement),
    graphics: one(body, "input.graphics", HTMLInputElement),
    result: one(body, "p.settings-result", HTMLParagraphElement),
  };
}

test("settings_section_cases", async () => {
  let reply: () => Promise<unknown> = () => Promise.resolve(settingsOf());
  const { calls, invoke } = recorder(() => reply());
  const section = createSettingsSection(invoke, () => Promise.resolve());
  const element = section.element;
  expect(element.tagName.toLowerCase()).toBe("section");
  expect(element.className).toBe("settings");

  // Rendered only for linux: a view of another OS leaves the section empty.
  section.render(settingsOf({ os: "windows" }));
  expect(element.children.length).toBe(0);
  section.render(settingsOf({ os: "macos" }));
  expect(element.children.length).toBe(0);

  // Linux: a `Settings` toggle opens the body.
  section.render(settingsOf());
  const toggle = one(element, ":scope > button.settings-toggle", HTMLButtonElement);
  expect(toggle.textContent).toBe("Settings");
  const body = one(element, ":scope > div.settings-body", HTMLDivElement);
  expect(body.hidden).toBe(true);
  toggle.click();
  expect(body.hidden).toBe(false);
  toggle.click();
  expect(body.hidden).toBe(true);
  const c = opened(element);
  expect(c.body.hidden).toBe(false);
  expect(c.root.type).toBe("text");
  expect(c.root.value).toBe(DEFAULT_ROOT);
  expect(c.root.disabled).toBe(false);
  expect(c.save.textContent).toBe("Save");
  expect(c.useDefault.textContent).toBe("Use default (~/.vst3)");
  expect(c.graphics.type).toBe("checkbox");
  expect(c.graphics.checked).toBe(false);
  const label = c.graphics.closest("label");
  expect(label?.textContent).toBe(GRAPHICS_LABEL);
  expect(element.querySelector("p.root-locked")).toBeNull();
  expect(c.result.textContent).toBe("");
  expect(calls).toEqual([]);

  // Save sends exactly the field text and the boolean.
  reply = () => Promise.resolve(settingsOf({ linux_plugin_root: OTHER_ROOT, root_is_default: false }));
  c.root.value = OTHER_ROOT;
  c.save.click();
  await settle();
  expect(calls).toEqual([["settings_save", { linuxPluginRoot: OTHER_ROOT, graphicsWorkaround: false }]]);
  expect(Object.keys(calls[0]?.[1] ?? {}).sort()).toEqual(["graphicsWorkaround", "linuxPluginRoot"]);
  expect(c.root.value).toBe(OTHER_ROOT);
  expect(c.result.textContent).toBe("Saved.");

  // Use default sends `null`, with the checkbox's state.
  reply = () => Promise.resolve(settingsOf({ graphics_workaround: true }));
  c.graphics.checked = true;
  c.useDefault.click();
  await settle();
  expect(calls[1]).toEqual(["settings_save", { linuxPluginRoot: null, graphicsWorkaround: true }]);
  expect(c.root.value).toBe(DEFAULT_ROOT);
  expect(c.graphics.checked).toBe(true);

  // A toggle-only Save over the default root keeps the stored default (`null`), never the effective path as text.
  reply = () => Promise.resolve(settingsOf());
  c.graphics.checked = false;
  c.save.click();
  await settle();
  expect(calls[2]).toEqual(["settings_save", { linuxPluginRoot: null, graphicsWorkaround: false }]);

  // A toggle-only Save over a custom root sends that root's text.
  section.render(settingsOf({ linux_plugin_root: OTHER_ROOT, root_is_default: false }));
  reply = () => Promise.resolve(settingsOf({ linux_plugin_root: OTHER_ROOT, root_is_default: false, graphics_workaround: true }));
  c.graphics.checked = true;
  c.save.click();
  await settle();
  expect(calls[3]).toEqual(["settings_save", { linuxPluginRoot: OTHER_ROOT, graphicsWorkaround: true }]);

  // The locked state (FR-041): the field disabled and the names; the toggle can still be saved.
  section.render(settingsOf({ root_locked_by: ["Alpha", "Beta"] }));
  expect(c.root.disabled).toBe(true);
  expect(one(element, "p.root-locked", HTMLParagraphElement).textContent).toBe(
    "Remove Alpha, Beta before changing the plug-in folder",
  );
  expect(element.querySelectorAll("p.root-locked").length).toBe(1);
  expect(c.save.disabled).toBe(false);
  expect(c.graphics.disabled).toBe(false);
  section.render(settingsOf());
  expect(c.root.disabled).toBe(false);
  expect(element.querySelector("p.root-locked")).toBeNull();

  // An E_SETTINGS_* rejection is shown in p.settings-result; the view stays and the controls come back.
  const pending: { reject: (reason: Error) => void } = { reject: () => undefined };
  reply = () =>
    new Promise((_resolve, reject) => {
      pending.reject = reject;
    });
  c.root.value = OTHER_ROOT;
  c.save.click();
  await settle();
  expect([c.root.disabled, c.save.disabled, c.useDefault.disabled, c.graphics.disabled]).toEqual([true, true, true, true]);
  c.save.click();
  await settle();
  expect(calls.length).toBe(5);
  const busy = "E_SETTINGS_BUSY: a plugin action is running";
  pending.reject(new Error(busy));
  await settle();
  expect(c.result.textContent).toBe(busy);
  expect([c.root.disabled, c.save.disabled, c.useDefault.disabled, c.graphics.disabled]).toEqual([false, false, false, false]);
  expect(c.root.value).toBe(OTHER_ROOT);

  // A result that fails its parser is a rejection line too.
  reply = () => Promise.resolve({ os: "linux" });
  c.save.click();
  await settle();
  expect(c.result.textContent.startsWith("$.")).toBe(true);
  expect(c.save.disabled).toBe(false);

  // The startup settings error (FR-040 `error`) is shown on render.
  section.render(settingsOf({ error: "E_SETTINGS_INVALID: the stored plug-in folder is not absolute" }));
  expect(c.result.textContent).toBe("E_SETTINGS_INVALID: the stored plug-in folder is not absolute");
  expect(calls.every(([cmd]) => cmd === "settings_save")).toBe(true);
});

test("settings_root_change_rereads_status", async () => {
  let reply: unknown = settingsOf();
  const { calls, invoke } = recorder(() => Promise.resolve(reply));
  let rereads = 0;
  const section = createSettingsSection(invoke, () => {
    rereads += 1;
    return Promise.resolve();
  });
  section.render(settingsOf());
  const c = opened(section.element);

  // A toggle-only save: the returned root equals the rendered one, so no re-read.
  reply = settingsOf({ graphics_workaround: true });
  c.graphics.checked = true;
  c.save.click();
  await settle();
  expect(calls.length).toBe(1);
  expect(rereads).toBe(0);

  // A saved root change: exactly one re-read.
  reply = settingsOf({ linux_plugin_root: OTHER_ROOT, root_is_default: false, graphics_workaround: true });
  c.root.value = OTHER_ROOT;
  c.save.click();
  await settle();
  expect(calls.length).toBe(2);
  expect(rereads).toBe(1);

  // Saving the same root again changes nothing.
  c.save.click();
  await settle();
  expect(calls.length).toBe(3);
  expect(rereads).toBe(1);

  // A refused root change re-reads nothing.
  const { invoke: refusing } = recorder(() => Promise.reject(new Error("E_SETTINGS_ROOT_IN_USE: remove Alpha first")));
  let refusedRereads = 0;
  const refused = createSettingsSection(refusing, () => {
    refusedRereads += 1;
    return Promise.resolve();
  });
  refused.render(settingsOf());
  const r = opened(refused.element);
  r.root.value = OTHER_ROOT;
  r.save.click();
  await settle();
  expect(r.result.textContent).toBe("E_SETTINGS_ROOT_IN_USE: remove Alpha first");
  expect(refusedRereads).toBe(0);
});
