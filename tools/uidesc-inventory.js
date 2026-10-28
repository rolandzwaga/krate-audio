#!/usr/bin/env node
// uidesc-inventory.js — a structured inventory of a VSTGUI editor.uidesc, plus a geometry and control-binding check.
//
// Inventory (JSON): per template, every view with its class, uidesc-relative and template-absolute rectangle,
// control-tag, title / fieldset-title / custom-view-name, parent, and per-template consistency metrics (classes, the
// distinct sizes each class is drawn at, the fonts and colors used). Written for the Iterum UI consistency workflow
// (.claude/workflows/iterum-ui-consistency.js), but nothing in it is Iterum-specific.
//
// Check (--check, exit 1 on a defect):
//   - overlap: two sibling views whose rectangles intersect. Sibling CONTAINERS at the same origin are reported as
//     "stacked" (visibility-switched variants) and are not a defect, and neither is any pair that includes the
//     UpdateBanner custom view (it overlays the header by design: the shared-editor-views rule makes it the last view),
//     nor a bipolar slider's centre tick (a childless 1-pt CViewContainer inside a draw-value-from-center CSlider).
//   - out-of-bounds: a view that extends past its parent container.
//   With --baseline <inventory.json> also:
//   - lost binding: a control-tag or custom-view-name bound in the baseline and bound nowhere now.
//   - lost control: a control-tag that had an interactive view in the baseline (any class but CParamDisplay and
//     CTextLabel) and has none now; a readout alone does not keep a parameter controllable.
//   - changed tag: a control-tag name whose numeric tag differs from the baseline (parameter IDs are frozen).
//
// Usage:  node tools/uidesc-inventory.js plugins/iterum/resources/editor.uidesc --out f:/tmp/inv.json
//         node tools/uidesc-inventory.js plugins/iterum/resources/editor.uidesc --check --baseline f:/tmp/inv.json
//         node tools/uidesc-inventory.js --self-test
'use strict';
const fs = require('fs');
const path = require('path');
const { XMLParser } = require('fast-xml-parser');

const READOUT_CLASSES = new Set(['CParamDisplay', 'CTextLabel']);

function fail(msg) {
  console.error(`uidesc-inventory: ${msg}`);
  process.exit(2);
}

const pair = (s) => {
  if (!s) return null;
  const [a, b] = String(s).split(',').map((v) => Number(v.trim()));
  return Number.isFinite(a) && Number.isFinite(b) ? [a, b] : null;
};
const isContainer = (v) => Array.isArray(v.view) && v.view.length > 0;
const inside = (inner, outer) => inner[0] >= outer[0] && inner[1] >= outer[1] &&
  inner[0] + inner[2] <= outer[0] + outer[2] && inner[1] + inner[3] <= outer[1] + outer[3];
const intersects = (a, b) => a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];

function buildInventory(xmlText, fileLabel) {
  const parser = new XMLParser({
    ignoreAttributes: false,
    attributeNamePrefix: '',
    parseAttributeValue: false,
    // Elements only: the "control-tag" ATTRIBUTE must stay a string.
    isArray: (name, jpath, isLeafNode, isAttribute) =>
      !isAttribute && ['template', 'view', 'control-tag', 'color', 'font', 'bitmap'].includes(name),
  });
  const doc = parser.parse(xmlText)['vstgui-ui-description'];
  if (!doc) throw new Error(`${fileLabel} has no <vstgui-ui-description> root`);

  const tags = {};
  for (const t of (doc['control-tags'] && doc['control-tags']['control-tag']) || []) tags[t.name] = Number(t.tag);

  const templates = {};
  const defects = [];
  for (const t of doc.template || []) {
    const views = [];
    const stacked = [];
    const walk = (node, parentIndex, offset, parentSize, depth) => {
      const placed = [];
      for (const v of node.view || []) {
        const origin = pair(v.origin) || [0, 0];
        const size = pair(v.size) || [0, 0];
        const abs = [offset[0] + origin[0], offset[1] + origin[1], size[0], size[1]];
        const index = views.length;
        views.push({
          index, parent: parentIndex, depth, class: v.class || null,
          origin, size, abs,
          controlTag: v['control-tag'] || null,
          tag: v['control-tag'] && v['control-tag'] in tags ? tags[v['control-tag']] : null,
          title: v.title || null,
          fieldsetTitle: v['fieldset-title'] || null,
          customViewName: v['custom-view-name'] || null,
          templateRef: v.template || v['template-names'] || null,
          font: v.font || null,
          drawValueFromCenter: v['draw-value-from-center'] === 'true',
          colors: Object.fromEntries(Object.entries(v).filter(([k]) => /color$/.test(k))),
          container: isContainer(v),
        });
        placed.push({ index, rect: [origin[0], origin[1], size[0], size[1]], container: isContainer(v) });
        if (parentSize && (origin[0] < 0 || origin[1] < 0 ||
            origin[0] + size[0] > parentSize[0] || origin[1] + size[1] > parentSize[1])) {
          defects.push({ kind: 'out-of-bounds', template: t.name, view: index,
            detail: `${v.class} ${v['control-tag'] || v.title || v['fieldset-title'] || ''} at ${origin} size ${size} in parent ${parentSize}` });
        }
        if (isContainer(v)) walk(v, index, [abs[0], abs[1]], size, depth + 1);
      }
      // A bipolar slider's centre tick: a childless CViewContainer 1 pt wide, wholly inside a CSlider that fills from
      // its centre (design-system.md 4.4). Nothing else is exempt.
      const isCentreTick = (t, s) => views[t.index].class === 'CViewContainer' && !t.container && t.rect[2] === 1 &&
        views[s.index].class === 'CSlider' && views[s.index].drawValueFromCenter && inside(t.rect, s.rect);
      const name = (k) => { const w = views[k]; return `${w.class} ${w.controlTag || w.title || w.fieldsetTitle || w.customViewName || ''}`.trim(); };
      for (let i = 0; i < placed.length; ++i) {
        for (let j = i + 1; j < placed.length; ++j) {
          const a = placed[i], b = placed[j];
          if (!intersects(a.rect, b.rect)) continue;
          if (views[a.index].customViewName === 'UpdateBanner' || views[b.index].customViewName === 'UpdateBanner') continue;
          if (isCentreTick(a, b) || isCentreTick(b, a)) continue;
          if (a.container && b.container && a.rect[0] === b.rect[0] && a.rect[1] === b.rect[1]) {
            stacked.push({ views: [a.index, b.index], detail: `${name(a.index)} / ${name(b.index)}` });
          } else {
            defects.push({ kind: 'overlap', template: t.name, views: [a.index, b.index],
              detail: `${name(a.index)} at ${a.rect} / ${name(b.index)} at ${b.rect}` });
          }
        }
      }
    };
    const tsize = pair(t.size);
    walk(t, null, [0, 0], tsize, 0);

    const sizesByClass = {};
    const fonts = new Set();
    const colors = new Set();
    for (const v of views) {
      const k = v.class || '?';
      sizesByClass[k] = sizesByClass[k] || {};
      sizesByClass[k][v.size.join('x')] = (sizesByClass[k][v.size.join('x')] || 0) + 1;
      if (v.font) fonts.add(v.font);
      Object.values(v.colors).forEach((c) => colors.add(c));
    }
    templates[t.name] = {
      stacked, class: t.class || null, size: tsize, views,
      metrics: {
        viewCount: views.length,
        boundControls: views.filter((v) => v.controlTag).length,
        sizesByClass,
        fonts: [...fonts].sort(),
        colors: [...colors].sort(),
      },
    };
  }
  return { file: fileLabel, controlTags: tags, templates, defects };
}

const allViews = (inv) => Object.values(inv.templates).flatMap((t) => t.views);

function bindings(inv) {
  const set = new Set();
  for (const v of allViews(inv)) {
    if (v.controlTag) set.add(`control-tag:${v.controlTag}`);
    if (v.customViewName) set.add(`custom-view:${v.customViewName}`);
  }
  return set;
}

// String(): inventories written before 2026-10-28 (inventory-before.json among them) store controlTag as a one-element
// array; String() reads both forms as the tag name.
function interactiveTags(inv) {
  return new Set(allViews(inv).filter((v) => v.controlTag && !READOUT_CLASSES.has(v.class)).map((v) => String(v.controlTag)));
}

/// Appends the baseline comparisons to inv.defects.
function compareToBaseline(inv, base) {
  const now = bindings(inv);
  for (const b of bindings(base)) if (!now.has(b)) inv.defects.push({ kind: 'lost-binding', detail: b });
  const controls = interactiveTags(inv);
  for (const tag of interactiveTags(base)) {
    if (!controls.has(tag)) inv.defects.push({ kind: 'lost-control', detail: `control-tag:${tag} has no interactive view` });
  }
  for (const [name, tag] of Object.entries(base.controlTags)) {
    if (name in inv.controlTags && inv.controlTags[name] !== tag) {
      inv.defects.push({ kind: 'changed-tag', detail: `${name}: ${tag} -> ${inv.controlTags[name]}` });
    }
  }
}

function selfTest() {
  const doc = (body) => `<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
  <control-tags><control-tag name="Mix" tag="7"/><control-tag name="Time" tag="8"/></control-tags>
  ${body}
</vstgui-ui-description>`;
  const kinds = (inv) => inv.defects.map((d) => d.kind).sort().join(',');
  const cases = [];
  const expect = (name, got, want) => cases.push({ name, ok: got === want, got, want });

  // The UpdateBanner overlays the header by design: no overlap defect. Any other overlap still is one.
  const banner = buildInventory(doc(`<template name="Editor" size="400, 300">
    <view class="CViewContainer" origin="0, 0" size="400, 50"><view class="CTextLabel" origin="0, 0" size="10, 10"/></view>
    <view custom-view-name="UpdateBanner" origin="0, 0" size="400, 60"/>
  </template>`), 'banner');
  expect('banner overlay is not a defect', kinds(banner), '');
  const plain = buildInventory(doc(`<template name="Editor" size="400, 300">
    <view class="CViewContainer" origin="0, 0" size="400, 50"><view class="CTextLabel" origin="0, 0" size="10, 10"/></view>
    <view class="CTextLabel" origin="0, 0" size="400, 60"/>
  </template>`), 'plain');
  expect('other overlaps still are', kinds(plain), 'overlap');

  // The bipolar centre tick: a childless 1-pt CViewContainer drawn over a draw-value-from-center CSlider's track.
  const tick = (sliderAttrs, tickRect) => buildInventory(doc(`<template name="P" size="400, 300">
    <view class="CSlider" origin="12, 22" size="260, 20" control-tag="Mix" ${sliderAttrs}/>
    <view class="CViewContainer" ${tickRect} background-color="text-dim" mouse-enabled="false"/>
  </template>`), 'tick');
  expect('centre tick over a bipolar slider is not a defect',
    kinds(tick('draw-value-from-center="true"', 'origin="142, 22" size="1, 20"')), '');
  expect('a tick over a unipolar slider still is', kinds(tick('', 'origin="142, 22" size="1, 20"')), 'overlap');
  expect('a tick wider than 1 pt still is',
    kinds(tick('draw-value-from-center="true"', 'origin="142, 22" size="2, 20"')), 'overlap');
  expect('a tick poking out of its slider still is',
    kinds(tick('draw-value-from-center="true"', 'origin="142, 20" size="1, 24"')), 'overlap');

  // Lost control: the slider is gone and only the readout keeps the tag bound.
  const base = buildInventory(doc(`<template name="P" size="400, 300">
    <view class="CSlider" origin="0, 0" size="100, 20" control-tag="Mix"/>
    <view class="CParamDisplay" origin="0, 30" size="40, 16" control-tag="Mix"/>
  </template>`), 'base');
  const readoutOnly = buildInventory(doc(`<template name="P" size="400, 300">
    <view class="CParamDisplay" origin="0, 30" size="40, 16" control-tag="Mix"/>
  </template>`), 'readout-only');
  compareToBaseline(readoutOnly, base);
  expect('readout alone is a lost control', kinds(readoutOnly), 'lost-control');
  const swapped = buildInventory(doc(`<template name="Q" size="400, 300">
    <view class="ArcKnob" origin="0, 0" size="40, 40" control-tag="Mix"/>
  </template>`), 'swapped');
  compareToBaseline(swapped, base);
  expect('a different widget keeps the control', kinds(swapped), '');
  const gone = buildInventory(doc(`<template name="P" size="400, 300"><view class="CTextLabel" origin="0, 0" size="10, 10"/></template>`), 'gone');
  compareToBaseline(gone, base);
  expect('a fully removed tag is lost twice over', kinds(gone), 'lost-binding,lost-control');

  let failed = 0;
  for (const c of cases) if (!c.ok) { failed++; console.log(`FAIL ${c.name}: got "${c.got}", want "${c.want}"`); }
  console.log(failed ? `uidesc-inventory self-test: ${failed} of ${cases.length} FAILED` : `uidesc-inventory self-test: ${cases.length} cases OK`);
  process.exit(failed ? 1 : 0);
}

const args = process.argv.slice(2);
if (args.includes('--self-test')) selfTest();

const file = args.find((a, i) => !a.startsWith('--') && !['--out', '--baseline'].includes(args[i - 1]));
if (!file) fail('usage: node tools/uidesc-inventory.js <editor.uidesc> [--out <json>] [--check] [--baseline <json>] | --self-test');
const optValue = (name) => (args.indexOf(name) >= 0 ? args[args.indexOf(name) + 1] : null);
const outPath = optValue('--out');
const baselinePath = optValue('--baseline');
const check = args.includes('--check');

let inventory;
try {
  inventory = buildInventory(fs.readFileSync(file, 'utf8'), path.relative(process.cwd(), file).replace(/\\/g, '/'));
} catch (e) {
  fail(e.message);
}
if (baselinePath) compareToBaseline(inventory, JSON.parse(fs.readFileSync(baselinePath, 'utf8')));

if (outPath) {
  fs.mkdirSync(path.dirname(path.resolve(outPath)), { recursive: true });
  fs.writeFileSync(outPath, JSON.stringify(inventory, null, 1));
}
const byKind = {};
for (const d of inventory.defects) byKind[d.kind] = (byKind[d.kind] || 0) + 1;
for (const [name, t] of Object.entries(inventory.templates)) {
  console.log(`${name}: ${t.metrics.viewCount} views, ${t.metrics.boundControls} bound, ${t.stacked.length} stacked pairs`);
}
for (const d of inventory.defects) console.log(`  ${d.kind}${d.template ? ` [${d.template}]` : ''}: ${d.detail}`);
console.log(`UIDESC ${inventory.defects.length ? 'DEFECTS' : 'CLEAN'} ${JSON.stringify(byKind)}`);
if (check && inventory.defects.length) process.exit(1);
