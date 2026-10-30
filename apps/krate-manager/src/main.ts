import { invoke } from "@tauri-apps/api/core";

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
}

const app = document.getElementById("app");
if (app !== null) void renderShell(app);
