#!/usr/bin/env node
// ==============================================================================
// remind-rt-safety.js  —  PostToolUse hook (Edit / Write / MultiEdit)
// ==============================================================================
// Skill auto-load is the one working moment-of-need mechanism, but whether the
// code-review / dsp-architecture skill fires is model judgment — an agent can
// edit an audio path without it triggering. This hook closes that gap: when a
// file under an audio-thread / DSP path is edited, it force-surfaces a one-time
// reminder to apply the real-time-safety review lens.
//
// Deliberately does NOT grep for new/malloc/lock — those appear legitimately in
// prepare()/ctors/member decls, so a content grep produces false-positive noise
// the agent learns to ignore. This only nudges toward the review; it does not
// judge the code. It is non-blocking (exit 0) and fires at most once per session.
//
// Reads the PostToolUse JSON envelope on stdin; emits an additionalContext note
// via hookSpecificOutput. Any parse failure exits 0 (fail-open — a hook must
// never wedge the session).
// ==============================================================================
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

function readStdin() {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (c) => (data += c));
    process.stdin.on('end', () => resolve(data));
    if (process.stdin.isTTY) resolve('');
  });
}

// Paths where an edit plausibly touches the audio thread or DSP math.
function isAudioPath(normalized) {
  return (
    /\/src\/processor\//.test(normalized) ||
    /^plugins\/[^/]+\/src\/processor\//.test(normalized) ||
    /\/src\/engine\//.test(normalized) ||
    /^plugins\/[^/]+\/src\/engine\//.test(normalized) ||
    /(^|\/)dsp\/include\//.test(normalized) ||
    /(^|\/)dsp\/(?!tests\/)/.test(normalized)
  );
}

(async () => {
  let payload;
  try {
    payload = JSON.parse(await readStdin());
  } catch {
    process.exit(0); // fail-open
  }

  const input = (payload && payload.tool_input) || {};
  const filePath = input.file_path || input.path || '';
  const normalized = String(filePath).replace(/\\/g, '/');
  if (!filePath || !isAudioPath(normalized)) process.exit(0);

  // Fire at most once per session: marker keyed by session_id in the temp dir.
  const session = String((payload && payload.session_id) || 'nosession').replace(/[^\w.-]/g, '_');
  const marker = path.join(os.tmpdir(), `krate-rtsafety-${session}.marker`);
  try {
    if (fs.existsSync(marker)) process.exit(0);
    fs.writeFileSync(marker, ''); // touch — presence is the only signal
  } catch {
    // If we cannot write the marker, still surface the reminder once (best effort).
  }

  const reminder =
    'Real-time-safety reminder (you just edited an audio-thread / DSP path): the audio ' +
    'thread has hard real-time constraints — no allocations, locks, exceptions, or I/O in ' +
    'process()/the render path. Before finishing, apply the RT-safety review lens ' +
    '(code-review skill -> DSP-REVIEW.md; canonical rules in dsp-architecture/REALTIME-SAFETY.md), ' +
    'and remember the DSP layer-dependency rule (a lint enforces it: node tools/lint-layers.js). ' +
    'This note fires once per session.';

  process.stdout.write(
    JSON.stringify({
      hookSpecificOutput: {
        hookEventName: 'PostToolUse',
        additionalContext: reminder,
      },
    })
  );
  process.exit(0);
})();
