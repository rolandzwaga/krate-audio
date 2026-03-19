#!/usr/bin/env node
// generate-signal-flow.js
// Reads a Graphviz .dot file and produces a self-contained HTML page
// that renders it client-side using @viz-js/viz (WebAssembly Graphviz).
//
// Usage: node tools/generate-signal-flow.js <input.dot> <output.html>

const fs = require("fs");
const path = require("path");

const [inputPath, outputPath] = process.argv.slice(2);

if (!inputPath || !outputPath) {
  console.error("Usage: node generate-signal-flow.js <input.dot> <output.html>");
  process.exit(1);
}

const dotSource = fs.readFileSync(inputPath, "utf-8");

// Derive plugin name from input path (e.g., "plugins/ruinae/docs/ruinae_signal_flow.dot" → "Ruinae")
const inputBasename = path.basename(inputPath, ".dot"); // "ruinae_signal_flow"
const pluginSlug = inputBasename.replace(/_signal_flow$/, ""); // "ruinae"
const pluginName = pluginSlug.charAt(0).toUpperCase() + pluginSlug.slice(1); // "Ruinae"

// Escape backticks and backslashes for embedding in a JS template literal
const escapedDot = dotSource.replace(/\\/g, "\\\\").replace(/`/g, "\\`").replace(/\$/g, "\\$");

const html = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>${pluginName} - Signal Flow Diagram</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --color-bg: #0a0a0f;
            --color-surface: #12121a;
            --color-border: #2a2a3a;
            --color-text: #e8e8f0;
            --color-text-muted: #8888a0;
            --color-accent: #a855f7;
            --color-accent-hover: #c084fc;
        }

        * { margin: 0; padding: 0; box-sizing: border-box; }

        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--color-bg);
            color: var(--color-text);
            line-height: 1.6;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
        }

        .header {
            padding: 1.5rem 2rem;
            border-bottom: 1px solid var(--color-border);
            display: flex;
            align-items: center;
            gap: 1.5rem;
            flex-shrink: 0;
        }

        .breadcrumb {
            font-size: 0.875rem;
        }

        .breadcrumb a {
            color: var(--color-text-muted);
            text-decoration: none;
            transition: color 0.2s;
        }

        .breadcrumb a:hover {
            color: var(--color-accent);
        }

        .breadcrumb .separator {
            color: var(--color-border);
            margin: 0 0.5rem;
        }

        h1 {
            font-size: 1.5rem;
            font-weight: 600;
            background: linear-gradient(135deg, var(--color-text) 0%, var(--color-accent) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }

        .toolbar {
            padding: 0.75rem 2rem;
            border-bottom: 1px solid var(--color-border);
            display: flex;
            align-items: center;
            gap: 0.75rem;
            background: var(--color-surface);
            flex-shrink: 0;
        }

        .toolbar button {
            padding: 0.375rem 0.75rem;
            background: var(--color-bg);
            border: 1px solid var(--color-border);
            border-radius: 6px;
            color: var(--color-text);
            font-family: inherit;
            font-size: 0.8rem;
            cursor: pointer;
            transition: all 0.2s;
        }

        .toolbar button:hover {
            border-color: var(--color-accent);
            color: var(--color-accent);
        }

        .toolbar .zoom-info {
            font-size: 0.8rem;
            color: var(--color-text-muted);
            margin-left: auto;
        }

        #diagram-container {
            flex: 1;
            overflow: hidden;
            position: relative;
            cursor: grab;
        }

        #diagram-container.grabbing {
            cursor: grabbing;
        }

        #diagram {
            position: absolute;
            transform-origin: 0 0;
        }

        #diagram svg {
            display: block;
        }

        #loading {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 100%;
            color: var(--color-text-muted);
            font-size: 1.1rem;
        }

        #error {
            display: none;
            padding: 2rem;
            color: #f87171;
            text-align: center;
        }

        @media (max-width: 640px) {
            .header { padding: 1rem; gap: 1rem; }
            .toolbar { padding: 0.5rem 1rem; }
            h1 { font-size: 1.25rem; }
        }
    </style>
</head>
<body>
    <div class="header">
        <nav class="breadcrumb">
            <a href="./">${pluginName}</a>
            <span class="separator">/</span>
        </nav>
        <h1>Signal Flow Diagram</h1>
    </div>

    <div class="toolbar">
        <button id="zoom-in" title="Zoom in">+</button>
        <button id="zoom-out" title="Zoom out">&minus;</button>
        <button id="zoom-fit" title="Fit to screen">Fit</button>
        <button id="zoom-reset" title="Reset to 100%">1:1</button>
        <span class="zoom-info" id="zoom-level">100%</span>
    </div>

    <div id="diagram-container">
        <div id="loading">Loading diagram&hellip;</div>
        <div id="error"></div>
        <div id="diagram"></div>
    </div>

    <script type="module">
        import { instance } from "https://cdn.jsdelivr.net/npm/@viz-js/viz@3.11.0/+esm";

        const dotSource = \`${escapedDot}\`;

        const container = document.getElementById("diagram-container");
        const diagramEl = document.getElementById("diagram");
        const loadingEl = document.getElementById("loading");
        const errorEl = document.getElementById("error");
        const zoomLevelEl = document.getElementById("zoom-level");

        let scale = 1;
        let panX = 0;
        let panY = 0;
        let isDragging = false;
        let startX, startY;

        function updateTransform() {
            diagramEl.style.transform = \`translate(\${panX}px, \${panY}px) scale(\${scale})\`;
            zoomLevelEl.textContent = Math.round(scale * 100) + "%";
        }

        function fitToScreen() {
            const svg = diagramEl.querySelector("svg");
            if (!svg) return;
            const svgW = svg.viewBox.baseVal.width || svg.width.baseVal.value;
            const svgH = svg.viewBox.baseVal.height || svg.height.baseVal.value;
            const cW = container.clientWidth;
            const cH = container.clientHeight;
            scale = Math.min(cW / svgW, cH / svgH, 1) * 0.95;
            panX = (cW - svgW * scale) / 2;
            panY = (cH - svgH * scale) / 2;
            updateTransform();
        }

        // Zoom controls
        document.getElementById("zoom-in").addEventListener("click", () => {
            scale = Math.min(scale * 1.25, 5);
            updateTransform();
        });
        document.getElementById("zoom-out").addEventListener("click", () => {
            scale = Math.max(scale / 1.25, 0.1);
            updateTransform();
        });
        document.getElementById("zoom-fit").addEventListener("click", fitToScreen);
        document.getElementById("zoom-reset").addEventListener("click", () => {
            scale = 1; panX = 0; panY = 0;
            updateTransform();
        });

        // Mouse wheel zoom
        container.addEventListener("wheel", (e) => {
            e.preventDefault();
            const rect = container.getBoundingClientRect();
            const mouseX = e.clientX - rect.left;
            const mouseY = e.clientY - rect.top;
            const factor = e.deltaY < 0 ? 1.1 : 1 / 1.1;
            const newScale = Math.min(Math.max(scale * factor, 0.1), 5);
            panX = mouseX - (mouseX - panX) * (newScale / scale);
            panY = mouseY - (mouseY - panY) * (newScale / scale);
            scale = newScale;
            updateTransform();
        }, { passive: false });

        // Pan with mouse drag
        container.addEventListener("mousedown", (e) => {
            isDragging = true;
            startX = e.clientX - panX;
            startY = e.clientY - panY;
            container.classList.add("grabbing");
        });
        window.addEventListener("mousemove", (e) => {
            if (!isDragging) return;
            panX = e.clientX - startX;
            panY = e.clientY - startY;
            updateTransform();
        });
        window.addEventListener("mouseup", () => {
            isDragging = false;
            container.classList.remove("grabbing");
        });

        // Touch support for pan
        let lastTouch = null;
        container.addEventListener("touchstart", (e) => {
            if (e.touches.length === 1) {
                lastTouch = { x: e.touches[0].clientX, y: e.touches[0].clientY };
            }
        });
        container.addEventListener("touchmove", (e) => {
            if (e.touches.length === 1 && lastTouch) {
                e.preventDefault();
                const dx = e.touches[0].clientX - lastTouch.x;
                const dy = e.touches[0].clientY - lastTouch.y;
                panX += dx;
                panY += dy;
                lastTouch = { x: e.touches[0].clientX, y: e.touches[0].clientY };
                updateTransform();
            }
        }, { passive: false });
        container.addEventListener("touchend", () => { lastTouch = null; });

        // Render
        try {
            const viz = await instance();
            const svg = viz.renderSVGElement(dotSource);
            svg.style.width = "auto";
            svg.style.height = "auto";
            loadingEl.style.display = "none";
            diagramEl.appendChild(svg);
            fitToScreen();
        } catch (err) {
            loadingEl.style.display = "none";
            errorEl.style.display = "block";
            errorEl.textContent = "Failed to render diagram: " + err.message;
        }
    </script>
</body>
</html>
`;

// Ensure output directory exists
const outDir = path.dirname(outputPath);
fs.mkdirSync(outDir, { recursive: true });

fs.writeFileSync(outputPath, html, "utf-8");
console.log(`Generated: ${outputPath}`);
