import { invoke } from "@tauri-apps/api/core";
import { createUpdateLine } from "./manager_update";
import { createSettingsSection } from "./settings";
import { mountStatus, tauriChannel } from "./status";
import type { StatusSection } from "./status";
import { toSettingsView } from "./view";
import type { SettingsView } from "./view";

export async function renderShell(root: HTMLElement): Promise<void> {
  root.replaceChildren();
  const title = document.createElement("h1");
  title.textContent = "Krate Manager";
  root.append(title);
  const notice = await invoke<string | null>("startup_error").catch(() => null);
  const line = document.createElement("p");
  if (notice !== null) {
    line.className = "notice";
    line.textContent = notice;
    root.append(line);
    return;
  }
  try {
    line.textContent = await invoke<string>("app_version");
  } catch {
    line.textContent = "version unavailable";
  }
  line.className = "version";
  root.append(line);
  // Phase 11 FR-024 to FR-026: the update line sits in the header and checks once the first plugin_status settles;
  // while the manager updates itself it switches off the mounted section's plugin actions.
  let status: StatusSection | null = null;
  const updateLine = createUpdateLine(invoke, {
    setEnabled(enabled) {
      status?.setActionsEnabled(enabled);
    },
  });
  root.append(updateLine.element);
  // Phase 12 PD-14 (FR-047, D8): the settings section is Linux-only. `settings_view` is read once, the first time a
  // settle finds a Linux status, and the section mounts below the status section; a saved root change re-reads it.
  let settingsRead = false;
  async function mountSettings(): Promise<void> {
    const section = status;
    if (settingsRead || section?.lastOs !== "linux") return;
    settingsRead = true;
    let view: SettingsView;
    try {
      view = toSettingsView(await invoke<unknown>("settings_view"));
    } catch {
      return; // a rejected or malformed view mounts nothing
    }
    const settings = createSettingsSection(invoke, () => section.refresh());
    settings.render(view);
    section.element.after(settings.element);
  }
  await mountStatus(
    root,
    invoke,
    tauriChannel,
    async () => {
      await updateLine.check();
      await mountSettings();
    },
    (section) => {
      status = section;
    },
  );
}

const app = document.getElementById("app");
if (app !== null) void renderShell(app);
