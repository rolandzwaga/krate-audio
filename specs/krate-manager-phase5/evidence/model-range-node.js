#!/usr/bin/env node
// SC-037 Node parity record for Krate Manager Phase 5 (tasks.md T016; plan.md §7.1 row SC-037, §7.4; spec FR-029, R-7,
// PD-8). Run by close-lane.js row A3e (stdout to evidence/model-range-node.txt) and once by hand at T016.
//
// WHY: feed::tests::model_range_cases asserts that six contract-VALID documents lose only the asset (or v1 entry) the
// Rust model cannot hold. "Contract-valid" must be the Node validator's verdict too, or the Rust client and the
// pipeline would disagree on the same bytes (FR-007 parity). This script applies the same six edits as
// apps/krate-manager/crates/core/src/feed_tests.rs `model_range_cases` (rows 1-6) to the fixtures in memory and prints
// validateFeed(doc, { schema }) (tools/feed-contract/contract.js:485) for each.
//
// Row 2 note: JSON.parse cannot tell `2.0` from `2`, so the Node side sees the number 2 either way; Ajv validates the
// parsed value, so feeding the text "2.0" would give the same verdict.
//
// Usage (anywhere; Node only, reads fixtures in place, writes nothing but stdout):
//   node specs/krate-manager-phase5/evidence/model-range-node.js
// Output: six lines `row <n> <schema> findings=<count>`; exit 0 when every count is 0, else 1.
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const FEED_CONTRACT = path.join(ROOT, 'tools', 'feed-contract');
const { validateFeed } = require(path.join(FEED_CONTRACT, 'contract.js'));
const FEEDS = path.join(FEED_CONTRACT, 'fixtures', 'feeds');
// The fixture asset URL prefix (feed_tests.rs `DOWNLOAD`, fixtures README "Synthetic values").
const DOWNLOAD = 'https://github.com/rolandzwaga/krate-audio/releases/download';

const doc = (rel) => JSON.parse(fs.readFileSync(path.join(FEEDS, rel), 'utf8'));

function rows() {
  // Row 1: payload.files 1e300.
  const row1 = doc('valid/full-v2.json');
  row1.plugins.Alpha.platforms['linux-x86_64'].payload.files = 1e300;
  // Row 2: size 2.0.
  const row2 = doc('valid/full-v2.json');
  row2.plugins.Alpha.platforms['windows-x86_64'].size = 2.0;
  // Row 3: Charlie gains a darwin-aarch64 copy of its darwin-universal asset whose min_os overflows u32.
  const row3 = doc('valid/full-v2.json');
  const copied = structuredClone(row3.plugins.Charlie.platforms['darwin-universal']);
  copied.min_os = '11.4294967296';
  row3.plugins.Charlie.platforms['darwin-aarch64'] = copied;
  // Row 4: every Alpha asset dropped by the Rust model.
  const row4 = doc('valid/full-v2.json');
  const alpha = row4.plugins.Alpha.platforms;
  alpha['darwin-aarch64'].min_os = '11.4294967296';
  alpha['linux-x86_64'].payload.files = 1e300;
  alpha['windows-x86_64'].url = `${DOWNLOAD}/alpha/v1.2.0/CON.exe`;
  // Row 5: an asset name that is an NTFS stream.
  const row5 = doc('valid/full-v2.json');
  row5.plugins.Bravo.platforms['windows-x86_64'].url = `${DOWNLOAD}/bravo/v2.0.1/x.exe:ads`;
  // Row 6: a v1 entry whose download_url fails the C1 url rule.
  const row6 = doc('valid/v1-only.json');
  row6.plugins.Bravo.download_url = 'javascript:x';
  return [[1, row1, 'v2'], [2, row2, 'v2'], [3, row3, 'v2'], [4, row4, 'v2'], [5, row5, 'v2'], [6, row6, 'v1']];
}

function main() {
  let bad = 0;
  for (const [n, d, schema] of rows()) {
    const { errors } = validateFeed(d, { schema });
    if (errors.length) bad++;
    console.log(`row ${n} ${schema} findings=${errors.length}`);
  }
  return bad ? 1 : 0;
}

process.exitCode = main();
