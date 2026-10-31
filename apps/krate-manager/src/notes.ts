// Release notes from the core's block model (spec FR-072; plan §4.10). Built with createElement and
// textContent only: no markup is parsed, no link or image exists, and the only property set from the
// data is `ol.start`.
import type { Block, Inline } from "./view";

function element<K extends keyof HTMLElementTagNameMap>(tag: K, children: readonly Node[]): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  e.append(...children);
  return e;
}

function text(value: string): Text {
  return document.createTextNode(value);
}

function renderInlines(inlines: readonly Inline[]): Node[] {
  return inlines.map((inline): Node => {
    switch (inline.type) {
      case "text":
        return text(inline.text);
      case "strong":
        return element("strong", renderInlines(inline.inlines));
      case "emphasis":
        return element("em", renderInlines(inline.inlines));
      case "code":
        return element("code", [text(inline.text)]);
      case "break":
        return element("br", []);
      default: {
        const unhandled: never = inline;
        return unhandled;
      }
    }
  });
}

/** `Heading` level L → `h{min(L + 2, 6)}` (FR-072). */
function heading(level: number): "h3" | "h4" | "h5" | "h6" {
  if (level <= 1) return "h3";
  if (level === 2) return "h4";
  if (level === 3) return "h5";
  return "h6";
}

export function renderBlocks(blocks: readonly Block[]): Node[] {
  return blocks.map((block): Node => {
    switch (block.type) {
      case "heading":
        return element(heading(block.level), renderInlines(block.inlines));
      case "paragraph":
        return element("p", renderInlines(block.inlines));
      case "list": {
        const items = block.items.map((item) => element("li", renderBlocks(item)));
        if (!block.ordered) return element("ul", items);
        const ol = element("ol", items);
        if (block.start !== null) ol.start = block.start;
        return ol;
      }
      case "quote":
        return element("blockquote", renderBlocks(block.blocks));
      case "code":
        return element("pre", [element("code", [text(block.text)])]);
      case "rule":
        return element("hr", []);
      default: {
        const unhandled: never = block;
        return unhandled;
      }
    }
  });
}
