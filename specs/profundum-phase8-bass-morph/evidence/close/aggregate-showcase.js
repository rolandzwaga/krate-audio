// Aggregate the B7-2 showcase-B harvest shards (evidence/harvest/showcase-*.txt) into
// evidence/showcase-harvest-aggregate.txt and print the ranking. Pick = the OK candidate with the largest
// min(worst_floor_lock, worst_floor_blend); ties broken by the smaller SC-015 Anchor-clause margin, then by index.
// Usage: node specs/profundum-phase8-bass-morph/evidence/close/aggregate-showcase.js
const fs = require('fs');
const path = require('path');
const dir = path.join(__dirname, '..', 'harvest');
const files = fs.readdirSync(dir).filter(f => /^showcase-\d+\.txt$/.test(f)).sort();
if (files.length === 0) throw new Error('no shard files in ' + dir);
const cols = ['cand', 'recipe', 'fold', 'shape', 'tone', 'amount', 'level', 'worst_floor_lock', 'worst_floor_blend',
  'sc015_anchor_E1', 'sc015_anchor_C3', 'dcx_travel_E1', 'dnl_travel_E1', 'dst_travel_E1', 'dbr_travel_E1',
  'dcx_travel_C3', 'dnl_travel_C3', 'dst_travel_C3', 'dbr_travel_C3', 'anchor_f0_p1_E1', 'verdict'];
const cands = [];
let rows = 0;
for (const f of files) {
  for (const line of fs.readFileSync(path.join(dir, f), 'utf8').split(/\r?\n/)) {
    if (line.startsWith('ROW\t')) rows++;
    if (!line.startsWith('CAND\t')) continue;
    const parts = line.split('\t').slice(1);
    if (parts.length !== cols.length) throw new Error(`${f}: CAND line has ${parts.length} fields, expected ${cols.length}`);
    const c = {};
    cols.forEach((k, i) => { c[k] = (k === 'recipe' || k === 'verdict') ? parts[i] : Number(parts[i]); });
    c.floor = Math.min(c.worst_floor_lock, c.worst_floor_blend);
    c.anchorClause = Math.min(c.sc015_anchor_E1, c.sc015_anchor_C3);
    c.travelMin = Math.min(c.dcx_travel_E1 - 3, c.dnl_travel_E1 - 3, c.dst_travel_E1 - 3, c.dbr_travel_E1 - 0.25,
      c.dcx_travel_C3 - 3, c.dnl_travel_C3 - 3, c.dst_travel_C3 - 3, c.dbr_travel_C3 - 0.25);
    cands.push(c);
  }
}
cands.sort((a, b) => a.cand - b.cand);
const expected = 96;
if (cands.length !== expected) throw new Error(`found ${cands.length} candidates, expected ${expected}`);
const ok = cands.filter(c => c.verdict === 'OK');
// Pick rule (main loop, run 3): floor-first among candidates with a robust clearance of the point bars — Anchor-clause
// margin >= 0.5 dB and travelMin >= 0.2 — so a hair-thin pass (cand 63: 0.2 dB / 0.04 oct) is not pinned as the fixture.
const robust = ok.filter(c => c.anchorClause >= 0.5 && c.travelMin >= 0.2);
const pool = robust.length ? robust : ok;
const ranked = [...pool].sort((a, b) => b.floor - a.floor || b.anchorClause - a.anchorClause || a.cand - b.cand);
const fmt = n => (typeof n === 'number' ? n.toFixed(3) : n);
let out = `# B7-2 showcase-B harvest aggregate (${files.length} shards, ${cands.length} candidates, ${rows} rows)\n`;
out += `# OK candidates: ${ok.length}; robust (Anchor clause >= 0.5 dB, travelMin >= 0.2): ${robust.length}. Ranking within the robust pool: floor = min(worst_floor_lock, worst_floor_blend) desc; then the SC-015 Anchor-clause margin desc.\n`;
out += '# rank\tcand\trecipe\tfold\tshape\ttone\tamount\tlevel\tfloor\tlock\tblend\tanchorClause\ttravelMin\tanchor_f0_p1_E1\n';
ranked.forEach((c, i) => {
  out += [i + 1, c.cand, c.recipe, c.fold, c.shape, c.tone, c.amount, c.level, fmt(c.floor), fmt(c.worst_floor_lock),
    fmt(c.worst_floor_blend), fmt(c.anchorClause), fmt(c.travelMin), fmt(c.anchor_f0_p1_E1)].join('\t') + '\n';
});
out += '\n# All candidates (index order)\n# cand\trecipe\tfold\tshape\ttone\tamount\tlevel\tfloor\tanchorClause\ttravelMin\tverdict\n';
for (const c of cands) {
  out += [c.cand, c.recipe, c.fold, c.shape, c.tone, c.amount, c.level, fmt(c.floor), fmt(c.anchorClause), fmt(c.travelMin), c.verdict].join('\t') + '\n';
}
if (ranked.length) {
  const p = ranked[0];
  out += `\nPICK cand ${p.cand}: recipe ${p.recipe} e1, fold ${p.fold}, shape ${p.shape}, tone ${p.tone}, amount ${p.amount}, anchor level ${p.level}; floor ${fmt(p.floor)} dB, anchor clause ${fmt(p.anchorClause)} dB, travel min ${fmt(p.travelMin)}\n`;
} else {
  out += '\nPICK none: no candidate meets the floor, the Anchor clause and the travel bars together\n';
}
fs.writeFileSync(path.join(__dirname, '..', 'showcase-harvest-aggregate.txt'), out);
process.stdout.write(out.split('\n').slice(0, 20).join('\n') + '\n...\n' + out.slice(out.lastIndexOf('PICK')));
