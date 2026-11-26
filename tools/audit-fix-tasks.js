#!/usr/bin/env node
// ==============================================================================
// audit-fix-tasks.js — the machine-readable task list of an audit fix plan
// ==============================================================================
// An audit (e.g. .claude/workflows/innexus-audit.js) writes one brief per fix
// task: <dir>/T<phase>.<n>.md. Each brief opens with ONE fenced json block, the
// task record. That block is the only source of the task list: the fix workflow
// (.claude/workflows/innexus-fix.js) schedules tasks from it, and a person who
// adds or changes a task edits the brief.
//
//   node tools/audit-fix-tasks.js <dir>                    every task, validated
//   node tools/audit-fix-tasks.js <dir> --phase 2          one phase
//   node tools/audit-fix-tasks.js <dir> --tasks T1.3,T2.1  named tasks
//   node tools/audit-fix-tasks.js <dir> --expect T1.1,...  also require exactly these ids
//   node tools/audit-fix-tasks.js --self-test
//
// Prints {"tasks":[...]} (compact; --pretty to indent), ordered by phase then
// number. Validation always covers the WHOLE directory, so a prerequisite in
// another phase is checked too. Exit 1 with one line per problem on stderr.
//
// Task record:
//   task_id        "T<phase>.<n>", equal to the file name
//   phase          the <phase> of the id
//   title          one line
//   finding_ids    the audit findings the task fixes; a finding belongs to one task
//   prerequisites  task ids whose CODE this task needs (not mere file overlap)
//   flags          upper-case flag names defined in the plan
//   model          "opus" | "sonnet": who implements it
//   exclusive      true = runs alone (regenerates presets, changes the build, needs the timing lane)
//   ruling         "none" | "before" | "after_verification" | "after_result"
//   ruling_question  the question for the user (required unless ruling is "none")
//   estimate_min   minutes
//   touches        [{ file, symbols }]: existing files the task edits. symbols are function names or
//                  named regions of a long function; "*" = the whole file; "+" = only adds lines
//                  (a test registration, a new member, a new case). Two tasks may run in parallel
//                  when they share no (file, symbol) pair other than "+".
//   creates        new files the task adds
// ==============================================================================
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

const ID = /^T(\d+)\.(\d+)$/;
const MODELS = ['opus', 'sonnet'];
const RULINGS = ['none', 'before', 'after_verification', 'after_result'];

const isStr = (v) => typeof v === 'string' && v.trim() !== '';
const isStrArray = (v) => Array.isArray(v) && v.every(isStr);
const isRepoPath = (v) => isStr(v) && !/^([a-zA-Z]:|\/|\.\.?\/)/.test(v) && !v.includes('\\');

/// The first fenced json block of a brief, parsed. Throws with a readable reason.
function recordOf(text) {
    const m = /^```json[ \t]*\r?\n([\s\S]*?)\r?\n```[ \t]*$/m.exec(text);
    if (!m) throw new Error('no ```json block');
    try {
        return JSON.parse(m[1]);
    } catch (e) {
        throw new Error(`the json block does not parse: ${e.message}`);
    }
}

function load(dir) {
    const problems = [];
    const tasks = [];
    let names;
    try {
        names = fs.readdirSync(dir).filter((n) => /^T\d+\.\d+\.md$/.test(n));
    } catch (e) {
        return { tasks, problems: [`cannot read ${dir}: ${e.message}`] };
    }
    if (names.length === 0) problems.push(`${dir}: no T<phase>.<n>.md brief found`);
    for (const name of names) {
        try {
            tasks.push({ name, record: recordOf(fs.readFileSync(path.join(dir, name), 'utf8')) });
        } catch (e) {
            problems.push(`${name}: ${e.message}`);
        }
    }
    return { tasks, problems };
}

function validate(loaded) {
    const problems = [...loaded.problems];
    const ids = new Set(loaded.tasks.map((t) => t.record && t.record.task_id));
    const findingOwner = new Map();
    for (const { name, record: r } of loaded.tasks) {
        const bad = (msg) => problems.push(`${name}: ${msg}`);
        if (r === null || typeof r !== 'object' || Array.isArray(r)) { bad('the json block is not an object'); continue; }
        const m = ID.exec(String(r.task_id));
        if (!m) bad(`task_id ${JSON.stringify(r.task_id)} is not T<phase>.<n>`);
        else {
            if (`${r.task_id}.md` !== name) bad(`task_id ${r.task_id} does not match the file name`);
            if (r.phase !== Number(m[1])) bad(`phase ${JSON.stringify(r.phase)} is not the phase of ${r.task_id}`);
        }
        if (!isStr(r.title)) bad('title is missing');
        if (!isStrArray(r.finding_ids) || r.finding_ids.length === 0) bad('finding_ids must be a non-empty list of ids');
        else for (const f of r.finding_ids) {
            if (findingOwner.has(f)) bad(`finding ${f} is also in ${findingOwner.get(f)}`);
            else findingOwner.set(f, r.task_id);
        }
        if (!isStrArray(r.prerequisites)) bad('prerequisites must be a list of task ids');
        else for (const p of r.prerequisites) {
            if (p === r.task_id) bad('a task cannot be its own prerequisite');
            else if (!ids.has(p)) bad(`prerequisite ${p} has no brief`);
        }
        if (!isStrArray(r.flags) || r.flags.some((f) => !/^[A-Z][A-Z -]*[A-Z]$/.test(f))) bad('flags must be a list of upper-case flag names');
        if (!MODELS.includes(r.model)) bad(`model must be one of ${MODELS.join(' / ')}`);
        if (typeof r.exclusive !== 'boolean') bad('exclusive must be true or false');
        if (!RULINGS.includes(r.ruling)) bad(`ruling must be one of ${RULINGS.join(' / ')}`);
        else if (r.ruling !== 'none' && !isStr(r.ruling_question)) bad(`ruling "${r.ruling}" needs a ruling_question`);
        if (typeof r.estimate_min !== 'number' || !(r.estimate_min > 0)) bad('estimate_min must be a positive number');
        if (!Array.isArray(r.touches)) bad('touches must be a list');
        else for (const t of r.touches) {
            if (!t || !isRepoPath(t.file)) bad(`touches: ${JSON.stringify(t && t.file)} is not a repo-relative path with forward slashes`);
            else if (!isStrArray(t.symbols) || t.symbols.length === 0) bad(`touches ${t.file}: symbols must name at least one function, region, "*" or "+"`);
        }
        if (!Array.isArray(r.creates) || !r.creates.every(isRepoPath)) bad('creates must be a list of repo-relative paths with forward slashes');
        if (Array.isArray(r.touches) && Array.isArray(r.creates) && r.touches.length + r.creates.length === 0) bad('a task that touches and creates nothing cannot be scheduled');
    }
    // A prerequisite cycle would leave its tasks waiting for each other forever.
    const byId = new Map(loaded.tasks.filter((t) => t.record && isStrArray(t.record.prerequisites)).map((t) => [t.record.task_id, t.record]));
    const state = new Map();
    const visit = (id, trail) => {
        if (state.get(id) === 'done' || !byId.has(id)) return;
        if (state.get(id) === 'open') { problems.push(`prerequisite cycle: ${[...trail.slice(trail.indexOf(id)), id].join(' -> ')}`); return; }
        state.set(id, 'open');
        for (const p of byId.get(id).prerequisites) visit(p, [...trail, id]);
        state.set(id, 'done');
    };
    for (const id of byId.keys()) visit(id, []);
    return problems;
}

const order = (a, b) => {
    const [, ap, an] = ID.exec(a.task_id) || [0, 0, 0];
    const [, bp, bn] = ID.exec(b.task_id) || [0, 0, 0];
    return ap - bp || an - bn;
};

function run(argv) {
    const dir = argv.find((a) => !a.startsWith('--') && argv[argv.indexOf(a) - 1] !== '--phase' && argv[argv.indexOf(a) - 1] !== '--tasks' && argv[argv.indexOf(a) - 1] !== '--expect');
    const opt = (name) => (argv.includes(name) ? argv[argv.indexOf(name) + 1] : undefined);
    if (!dir) return { code: 2, err: ['usage: node tools/audit-fix-tasks.js <tasks-dir> [--phase N | --tasks T1.1,T1.2] [--expect ids] [--pretty]'] };
    const loaded = load(dir);
    const problems = validate(loaded);
    const all = loaded.tasks.map((t) => t.record).filter((r) => r && typeof r === 'object' && ID.test(String(r.task_id))).sort(order);
    const expect = opt('--expect');
    if (expect !== undefined) {
        const want = expect.split(',').map((s) => s.trim()).filter(Boolean);
        const have = new Set(all.map((t) => t.task_id));
        for (const id of want) if (!have.has(id)) problems.push(`expected task ${id} has no brief`);
        for (const id of have) if (!want.includes(id)) problems.push(`brief ${id} is not an expected task`);
    }
    let selected = all;
    const phase = opt('--phase');
    const names = opt('--tasks');
    if (phase !== undefined) selected = all.filter((t) => t.phase === Number(phase));
    if (names !== undefined) {
        const want = names.split(',').map((s) => s.trim()).filter(Boolean);
        for (const id of want) if (!all.some((t) => t.task_id === id)) problems.push(`--tasks: ${id} has no brief`);
        selected = all.filter((t) => want.includes(t.task_id));
    }
    if (problems.length) return { code: 1, err: problems };
    return { code: 0, out: JSON.stringify({ tasks: selected }, null, argv.includes('--pretty') ? 2 : 0) };
}

function selfTest() {
    const assert = require('assert');
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'audit-fix-tasks-'));
    const base = {
        task_id: 'T1.1', phase: 1, title: 'a', finding_ids: ['INX-001'], prerequisites: [], flags: ['VERIFY-FIRST'], model: 'opus',
        exclusive: false, ruling: 'none', ruling_question: '', estimate_min: 30,
        touches: [{ file: 'plugins/innexus/src/processor/processor.cpp', symbols: ['process:master'] }], creates: [],
    };
    const write = (rec, name = `${rec.task_id}.md`) =>
        fs.writeFileSync(path.join(tmp, name), `# ${rec.task_id}\n\n\`\`\`json\n${JSON.stringify(rec, null, 2)}\n\`\`\`\n\nBrief text with a later block:\n\n\`\`\`json\n{"ignored": true}\n\`\`\`\n`);
    const reset = () => { for (const n of fs.readdirSync(tmp)) fs.rmSync(path.join(tmp, n)); };
    const errors = (...args) => run([tmp, ...args]).err || [];
    try {
        write(base);
        write({ ...base, task_id: 'T1.2', finding_ids: ['INX-002'], prerequisites: ['T1.1'], model: 'sonnet' });
        write({ ...base, task_id: 'T2.1', phase: 2, finding_ids: ['INX-003'], ruling: 'before', ruling_question: 'which?' });
        fs.writeFileSync(path.join(tmp, 'README.md'), 'not a brief');
        let r = run([tmp]);
        assert.strictEqual(r.code, 0, (r.err || []).join('\n'));
        assert.deepStrictEqual(JSON.parse(r.out).tasks.map((t) => t.task_id), ['T1.1', 'T1.2', 'T2.1']);
        assert.deepStrictEqual(JSON.parse(run([tmp, '--phase', '2']).out).tasks.map((t) => t.task_id), ['T2.1']);
        assert.deepStrictEqual(JSON.parse(run([tmp, '--tasks', 'T1.2,T2.1']).out).tasks.map((t) => t.task_id), ['T1.2', 'T2.1']);
        assert.strictEqual(run([tmp, '--expect', 'T1.1,T1.2,T2.1']).code, 0);
        assert.match(errors('--expect', 'T1.1,T1.2').join('\n'), /brief T2\.1 is not an expected task/);
        assert.match(errors('--expect', 'T1.1,T1.2,T2.1,T2.2').join('\n'), /expected task T2\.2 has no brief/);
        assert.match(errors('--tasks', 'T9.9').join('\n'), /T9\.9 has no brief/);

        const broken = [
            [{ ...base, task_id: 'T1.3', phase: 2 }, /phase 2 is not the phase of T1\.3/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-001'] }, /finding INX-001 is also in T1\.1|finding INX-001 is also in T1\.3/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], prerequisites: ['T7.7'] }, /prerequisite T7\.7 has no brief/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], model: 'haiku' }, /model must be one of/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], ruling: 'before' }, /needs a ruling_question/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], exclusive: 'no' }, /exclusive must be true or false/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], touches: [{ file: 'f:/x/processor.cpp', symbols: ['a'] }] }, /not a repo-relative path/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], touches: [{ file: 'a.cpp', symbols: [] }] }, /symbols must name at least one/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], touches: [], creates: [] }, /touches and creates nothing/],
            [{ ...base, task_id: 'T1.3', finding_ids: ['INX-009'], flags: ['lowercase'] }, /upper-case flag names/],
        ];
        for (const [rec, re] of broken) {
            write(rec);
            assert.match(errors().join('\n'), re);
            fs.rmSync(path.join(tmp, 'T1.3.md'));
        }
        write({ ...base, task_id: 'T1.3', finding_ids: ['INX-009'] }, 'T1.4.md');
        assert.match(errors().join('\n'), /task_id T1\.3 does not match the file name/);
        fs.rmSync(path.join(tmp, 'T1.4.md'));
        fs.writeFileSync(path.join(tmp, 'T1.3.md'), '# T1.3\n\nno block here\n');
        assert.match(errors().join('\n'), /T1\.3\.md: no ```json block/);
        fs.writeFileSync(path.join(tmp, 'T1.3.md'), '```json\n{ not json }\n```\n');
        assert.match(errors().join('\n'), /does not parse/);

        reset();
        write({ ...base, prerequisites: ['T1.2'] });
        write({ ...base, task_id: 'T1.2', finding_ids: ['INX-002'], prerequisites: ['T1.1'] });
        assert.match(errors().join('\n'), /prerequisite cycle: T1\.1 -> T1\.2 -> T1\.1|prerequisite cycle: T1\.2 -> T1\.1 -> T1\.2/);
        reset();
        assert.match(errors().join('\n'), /no T<phase>\.<n>\.md brief found/);
        assert.strictEqual(run([]).code, 2);
    } finally {
        fs.rmSync(tmp, { recursive: true, force: true });
    }
    console.log('audit-fix-tasks: self-test passed');
}

if (require.main === module) {
    const argv = process.argv.slice(2);
    if (argv.includes('--self-test')) {
        selfTest();
    } else {
        const r = run(argv);
        if (r.err) process.stderr.write(r.err.map((l) => `audit-fix-tasks: ${l}`).join('\n') + '\n');
        if (r.out) process.stdout.write(r.out + '\n');
        process.exit(r.code);
    }
}

module.exports = { run, recordOf };
