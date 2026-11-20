// generate-signal-flow.js
// Renders a plugin's signal-flow page (krateaudio.com/<plugin>/signal-flow.html) from its Graphviz .dot source.
// tools/gen-site.js calls renderSignalFlowPage() for every plugin with a plugins/<dir>/docs/<dir>_signal_flow.dot.
//
// With `svg` (the diagram pre-rendered at build time by @viz-js/viz) the page ships the diagram as inline SVG, so its
// stage names are page text and no WebAssembly loads in the browser; without it the page renders the .dot client-side
// with @viz-js/viz from the CDN, as it did before the site generator existed.

"use strict";

function escapeHtml(text) {
  return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

/**
 * The page's HTML.
 *   pluginName   "Ruinae"
 *   description  the meta description (also shown under the heading)
 *   canonical    the page's absolute URL
 *   headCommon   the shared <head> block (tools/site/common.js headCommon())
 *   dotSource    the .dot text
 *   svg          the pre-rendered diagram, or null to render in the browser
 *   hasManual    whether the plugin has a manual.html beside this page to link to
 */
function renderSignalFlowPage({ pluginName, description, canonical, headCommon, dotSource, svg, hasManual }) {
  const name = escapeHtml(pluginName);
  const title = `${name} Signal Flow Diagram | Krate Audio`;
  const desc = escapeHtml(description);
  // Escape backticks, backslashes and "${" for embedding the .dot source in a JS template literal.
  const escapedDot = dotSource.replace(/\\/g, "\\\\").replace(/`/g, "\\`").replace(/\$/g, "\\$");
  const render = svg
    ? `        const svg = diagramEl.querySelector("svg");
        svg.style.width = "auto";
        svg.style.height = "auto";
        loadingEl.style.display = "none";
        fitToScreen();`
    : `        try {
            const { instance } = await import("https://cdn.jsdelivr.net/npm/@viz-js/viz@3.11.0/+esm");
            const viz = await instance();
            const svg = viz.renderSVGElement(\`${escapedDot}\`);
            svg.style.width = "auto";
            svg.style.height = "auto";
            loadingEl.style.display = "none";
            diagramEl.appendChild(svg);
            fitToScreen();
        } catch (err) {
            loadingEl.style.display = "none";
            errorEl.style.display = "block";
            errorEl.textContent = "Failed to render diagram: " + err.message;
        }`;

  return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>${title}</title>
    <meta name="description" content="${desc}">
    <link rel="canonical" href="${escapeHtml(canonical)}">
    <meta property="og:title" content="${name} Signal Flow Diagram">
    <meta property="og:description" content="${desc}">
    <meta property="og:type" content="article">
    <meta property="og:url" content="${escapeHtml(canonical)}">
${headCommon}
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
            padding: 1.25rem 2rem;
            border-bottom: 1px solid var(--color-border);
            flex-shrink: 0;
        }

        .breadcrumb {
            font-size: 0.875rem;
            margin-bottom: 0.25rem;
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

        .intro {
            color: var(--color-text-muted);
            font-size: 0.9rem;
            max-width: 70ch;
        }

        .intro a {
            color: var(--color-accent-hover);
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
            min-height: 60vh;
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
            .header { padding: 1rem; }
            .toolbar { padding: 0.5rem 1rem; }
            h1 { font-size: 1.25rem; }
        }
    </style>
</head>
<body>
    <div class="header">
        <nav class="breadcrumb">
            <a href="/">Krate Audio</a>
            <span class="separator">/</span>
            <a href="./">${name}</a>
            <span class="separator">/</span>
        </nav>
        <h1>${name} Signal Flow</h1>
        <p class="intro">${desc} Drag to pan, scroll to zoom.${hasManual ? ' The <a href="manual.html">user manual</a> explains each stage.' : ""}</p>
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
        <div id="diagram">${svg ?? ""}</div>
    </div>

    <script type="module">
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
${render}
    </script>
</body>
</html>
`;
}

module.exports = { renderSignalFlowPage };
