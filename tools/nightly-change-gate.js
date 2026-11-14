#!/usr/bin/env node
// Nightly change gate: should a scheduled nightly workflow run, given what changed since its last real run?
//
// WHY: the nightlies (valgrind, long tests, sanitizers) each cost hours of runner time. They used to run whenever
// anything had been pushed to any branch in the last 24 h, so a docs commit, a feature branch or a Krate Manager
// change re-ran hours of memcheck and [long] renders over a tree whose relevant code had not changed (user, 2026-11-14).
//
// HOW: the base is the head commit of this workflow's most recent run in which a job other than the gate itself ran
// to completion (success or failure). A run the gate skipped tested nothing, and a cancelled run finished nothing, so
// neither counts. The workflow runs when `git diff <base> HEAD` touches any of its pathspecs (Markdown excluded).
// It fails open: no tested run in the last 30, an unreachable base or an API error means it runs. A
// workflow_dispatch always runs.
//
// Usage (workflow step; needs GH_TOKEN, a checkout with history, GITHUB_REPOSITORY, GITHUB_RUN_ID, GITHUB_EVENT_NAME):
//   node tools/nightly-change-gate.js --workflow <file.yml> -- <pathspec> ...
//   node tools/nightly-change-gate.js --self-test
// Writes should_run=true|false to $GITHUB_OUTPUT and the reason to $GITHUB_STEP_SUMMARY.
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');

/// Names the gate job had before this script existed (the pushed_at gate, until 2026-11-14): those runs' gate job
/// must not count as a job that tested.
const kLegacyGateJobs = ['Check for recent pushes'];

/// The head SHA of the newest run (other than `currentRunId`) in which some job that is not a gate job concluded
/// success or failure, or null. `runs` newest first: [{ id, head_sha }]; jobsOf(id) -> [{ name, conclusion }];
/// gateJobs: a Set of gate job names.
function pickBase(runs, jobsOf, gateJobs, currentRunId) {
    for (const r of runs) {
        if (String(r.id) === String(currentRunId)) continue;
        const tested = jobsOf(r.id).some((j) => !gateJobs.has(j.name) && (j.conclusion === 'success' || j.conclusion === 'failure'));
        if (tested) return r.head_sha;
    }
    return null;
}

/// The decision. changed: the relevant files changed since base (null when the diff could not be taken).
function decide({ event, base, changed }) {
    if (event !== 'schedule') return { run: true, reason: `manual run (${event}): the change gate is not applied` };
    if (base === null) return { run: true, reason: 'no completed tested run of this workflow in the last 30: running' };
    if (changed === null) return { run: true, reason: `could not diff against the last tested commit ${base.slice(0, 12)}: running` };
    if (changed.length === 0) return { run: false, reason: `no relevant file changed since the last tested commit ${base.slice(0, 12)}: skipping` };
    const shown = changed.slice(0, 10).join(', ') + (changed.length > 10 ? `, ... (${changed.length} files)` : '');
    return { run: true, reason: `${changed.length} relevant file(s) changed since ${base.slice(0, 12)}: ${shown}` };
}

function selfTest() {
    const jobs = {
        5: [{ name: 'Gate', conclusion: 'success' }, { name: 'Heavy', conclusion: 'skipped' }],
        4: [{ name: 'Gate', conclusion: 'success' }, { name: 'Heavy', conclusion: 'cancelled' }],
        3: [{ name: 'Gate', conclusion: 'success' }, { name: 'Heavy', conclusion: 'failure' }],
        2: [{ name: 'Gate', conclusion: 'success' }, { name: 'Heavy', conclusion: 'success' }],
        9: [{ name: 'Gate', conclusion: 'success' }, { name: 'Heavy', conclusion: 'success' }],
    };
    const runs = [{ id: 9, head_sha: 'current' }, { id: 5, head_sha: 'skipped' }, { id: 4, head_sha: 'cancelled' },
        { id: 3, head_sha: 'failed' }, { id: 2, head_sha: 'passed' }];
    const gate = new Set(['Gate', ...kLegacyGateJobs]);
    const cases = [
        ['skips the current, gate-skipped and cancelled runs; a failed run was tested', pickBase(runs, (id) => jobs[id], gate, 9), 'failed'],
        ['no tested run', pickBase(runs.slice(0, 3), (id) => jobs[id], gate, 9), null],
        ['only the gate job ran', pickBase([{ id: 7, head_sha: 'x' }], () => [{ name: 'Gate', conclusion: 'success' }], gate, 1), null],
        ['a legacy-named gate job is not a test', pickBase([{ id: 7, head_sha: 'x' }],
            () => [{ name: 'Check for recent pushes', conclusion: 'success' }, { name: 'Heavy', conclusion: 'skipped' }], gate, 1), null],
        ['dispatch always runs', decide({ event: 'workflow_dispatch', base: 'abc', changed: [] }).run, true],
        ['no base runs', decide({ event: 'schedule', base: null, changed: null }).run, true],
        ['diff failed runs', decide({ event: 'schedule', base: 'abc', changed: null }).run, true],
        ['nothing changed skips', decide({ event: 'schedule', base: 'abc', changed: [] }).run, false],
        ['a change runs', decide({ event: 'schedule', base: 'abc', changed: ['dsp/x.h'] }).run, true],
    ];
    let bad = 0;
    for (const [what, got, want] of cases) {
        const ok = got === want;
        if (!ok) bad += 1;
        console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`);
    }
    console.log(bad === 0 ? `nightly-change-gate self-test: OK (${cases.length})` : `nightly-change-gate self-test: ${bad} FAILED`);
    process.exit(bad === 0 ? 0 : 1);
}

function main() {
    const args = process.argv.slice(2);
    if (args.includes('--self-test')) selfTest();
    const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
    const workflow = opt('--workflow');
    const sep = args.indexOf('--');
    const paths = sep >= 0 ? args.slice(sep + 1) : [];
    if (!workflow || paths.length === 0) {
        console.error('usage: nightly-change-gate.js --workflow <file.yml> -- <pathspec> ...');
        process.exit(2);
    }
    const repo = process.env.GITHUB_REPOSITORY;
    const event = process.env.GITHUB_EVENT_NAME || 'workflow_dispatch';
    const gh = (endpoint) => JSON.parse(execFileSync('gh', ['api', endpoint], { encoding: 'utf8', maxBuffer: 64 << 20 }));

    let base = null;
    let changed = null;
    if (event === 'schedule') {
        try {
            // The gate job is the one this step runs in: the only job of the current run in progress (every other
            // job needs it), so its name never has to be kept in step with an argument.
            const gateJobs = new Set(kLegacyGateJobs);
            for (const j of gh(`repos/${repo}/actions/runs/${process.env.GITHUB_RUN_ID}/jobs?per_page=100`).jobs) {
                if (j.status === 'in_progress') gateJobs.add(j.name);
            }
            const runs = gh(`repos/${repo}/actions/workflows/${workflow}/runs?branch=main&status=completed&per_page=30`).workflow_runs;
            base = pickBase(runs, (id) => gh(`repos/${repo}/actions/runs/${id}/jobs?per_page=100`).jobs, gateJobs, process.env.GITHUB_RUN_ID);
        } catch (e) {
            console.log(`::warning::nightly-change-gate: listing the previous runs failed (${e.message.split('\n')[0]}); running`);
        }
        if (base !== null) {
            try {
                const out = execFileSync('git', ['diff', '--name-only', base, 'HEAD', '--', ...paths, ':(exclude)*.md'], { encoding: 'utf8' });
                changed = out.split('\n').filter(Boolean);
            } catch (e) {
                console.log(`::warning::nightly-change-gate: git diff against ${base} failed (${e.message.split('\n')[0]}); running`);
            }
        }
    }
    const d = decide({ event, base, changed });
    console.log(`nightly-change-gate: ${d.run ? 'RUN' : 'SKIP'} - ${d.reason}`);
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT, `should_run=${d.run}\n`);
    if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, `${d.run ? 'Running' : 'Skipping'}: ${d.reason}\n`);
}

main();
