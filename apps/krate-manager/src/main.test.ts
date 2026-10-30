import { afterEach, expect, test } from "vitest";
import { clearMocks, mockIPC } from "@tauri-apps/api/mocks";
import { renderShell } from "./main";

afterEach(clearMocks);

test("shows the title and the version", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") return null;
    if (cmd === "app_version") return "0.1.0";
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  expect(root.querySelector("p.version")?.textContent).toBe("0.1.0");
  expect(root.querySelector(".notice")).toBeNull();
  expect(calls).toEqual(["startup_error", "app_version"]);
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
  expect(calls).toEqual(["startup_error", "app_version"]);
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
  expect(calls).toEqual(["startup_error"]);
});

test("shows the version when startup_error fails", async () => {
  const root = document.createElement("main");
  const calls: string[] = [];
  mockIPC((cmd) => {
    calls.push(cmd);
    if (cmd === "startup_error") throw new Error("x");
    if (cmd === "app_version") return "0.1.0";
    throw new Error(`unexpected command ${cmd}`);
  });
  await renderShell(root);
  expect(root.querySelector("h1")?.textContent).toBe("Krate Manager");
  expect(root.querySelector("p.version")?.textContent).toBe("0.1.0");
  expect(root.querySelector(".notice")).toBeNull();
  expect(calls).toEqual(["startup_error", "app_version"]);
});
