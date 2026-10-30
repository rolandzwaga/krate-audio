import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import tseslint from "typescript-eslint";

const SINKS = "is banned: build DOM with createElement and textContent (spec FR-052)";
export default defineConfig(
  { ignores: ["dist/", "src-tauri/", "crates/", "target/", "tests/fixtures/", "eslint.config.mjs", "vite.config.ts"] },
  js.configs.recommended,
  tseslint.configs.strictTypeChecked,
  { languageOptions: { parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname } } },
  { linterOptions: { reportUnusedDisableDirectives: "error" } },
  {
    rules: {
      "no-restricted-properties": ["error",
        { property: "innerHTML", message: `innerHTML ${SINKS}` },
        { property: "outerHTML", message: `outerHTML ${SINKS}` },
        { property: "insertAdjacentHTML", message: `insertAdjacentHTML ${SINKS}` },
        { object: "document", property: "write", message: `document.write ${SINKS}` },
        { object: "document", property: "writeln", message: `document.writeln ${SINKS}` }],
      "no-restricted-syntax": ["error",
        { selector: "CallExpression[callee.name='eval']", message: `eval ${SINKS}` },
        { selector: "NewExpression[callee.name='Function']", message: `new Function ${SINKS}` }],
    },
  },
);
