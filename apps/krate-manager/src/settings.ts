// The Linux settings section (Phase 12 spec FR-047, FR-048; plan §4.7 `settings.ts` paragraph, PD-14). Every string
// comes from SETTINGS_TEXTS through `fill` and reaches the DOM through textContent; view data reaches only
// `input.value`, `input.checked` and `disabled`. The webview sends only the field text (or `null` for the default) and
// the boolean; core validates both (`settings::save_for_command`).
import { fill, rejectionText } from "./status";
import type { Invoker } from "./status";
import { toSettingsView } from "./view";
import type { SettingsView } from "./view";

/** FR-047's texts and the save result line. */
export const SETTINGS_TEXTS = {
  toggle: "Settings",
  rootLabel: "Plug-in folder",
  save: "Save",
  useDefault: "Use default (~/.vst3)",
  graphics: "Work around a blank or flickering window (NVIDIA): takes effect the next time Krate Manager starts",
  rootLocked: "Remove {names} before changing the plug-in folder",
  saved: "Saved.",
} as const satisfies Record<string, string>;

export interface SettingsSection {
  readonly element: HTMLElement;
  /** Shows `view`; a view of another OS than Linux renders nothing (D8). */
  render(view: SettingsView): void;
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
 * The section: a `Settings` toggle opening the root field, `Save`, `Use default`, the graphics checkbox, the locked
 * line and the result line. `onRootChanged` runs once after a save whose returned root differs from the shown one
 * (FR-048: the status is re-read).
 */
export function createSettingsSection(invoke: Invoker, onRootChanged: () => Promise<void>): SettingsSection {
  const element = document.createElement("section");
  element.className = "settings";
  const toggle = button("settings-toggle", SETTINGS_TEXTS.toggle);
  const body = document.createElement("div");
  body.className = "settings-body";
  body.hidden = true;
  const root = document.createElement("input");
  root.type = "text";
  root.className = "root";
  const rootLabel = document.createElement("label");
  rootLabel.className = "root-label";
  rootLabel.append(SETTINGS_TEXTS.rootLabel, " ", root);
  const save = button("save", SETTINGS_TEXTS.save);
  const useDefault = button("use-default", SETTINGS_TEXTS.useDefault);
  const graphics = document.createElement("input");
  graphics.type = "checkbox";
  graphics.className = "graphics";
  const graphicsLabel = document.createElement("label");
  graphicsLabel.className = "graphics-label";
  graphicsLabel.append(graphics, SETTINGS_TEXTS.graphics);
  const locked = paragraph("root-locked", "");
  const result = paragraph("settings-result", "");
  body.append(rootLabel, save, useDefault, graphicsLabel, result);
  toggle.addEventListener("click", () => {
    body.hidden = !body.hidden;
  });

  let shown: SettingsView | null = null;
  let saving = false;

  function setControls(view: SettingsView): void {
    root.disabled = saving || view.root_locked_by.length > 0;
    save.disabled = saving;
    useDefault.disabled = saving;
    graphics.disabled = saving;
  }

  function render(view: SettingsView): void {
    shown = view;
    if (view.os !== "linux") {
      element.replaceChildren();
      return;
    }
    if (toggle.parentNode === null) element.append(toggle, body);
    root.value = view.linux_plugin_root;
    graphics.checked = view.graphics_workaround;
    if (view.root_locked_by.length > 0) {
      locked.textContent = fill(SETTINGS_TEXTS.rootLocked, { names: view.root_locked_by.join(", ") });
      if (locked.parentNode === null) result.before(locked);
    } else {
      locked.remove();
    }
    result.textContent = view.error ?? "";
    setControls(view);
  }

  /** One `settings_save` with `linuxPluginRoot` (`null`: the default) and the checkbox's state. */
  async function submit(linuxPluginRoot: string | null): Promise<void> {
    const before = shown;
    if (saving || before === null) return;
    saving = true;
    setControls(before);
    let next: SettingsView | null = null;
    try {
      next = toSettingsView(await invoke<unknown>("settings_save", { linuxPluginRoot, graphicsWorkaround: graphics.checked }));
    } catch (reason) {
      result.textContent = rejectionText(reason);
    }
    saving = false;
    if (next === null) {
      setControls(before);
      return;
    }
    render(next);
    result.textContent = next.error ?? SETTINGS_TEXTS.saved;
    if (next.linux_plugin_root !== before.linux_plugin_root) await onRootChanged();
  }

  save.addEventListener("click", () => {
    // An untouched field over the default root saves the default again (`null`), not the effective path as a custom
    // root: core compares stored values (FR-043), so a toggle-only save never becomes a root change.
    const unchangedDefault = shown !== null && shown.root_is_default && root.value === shown.linux_plugin_root;
    void submit(unchangedDefault ? null : root.value);
  });
  useDefault.addEventListener("click", () => {
    void submit(null);
  });

  return { element, render };
}
