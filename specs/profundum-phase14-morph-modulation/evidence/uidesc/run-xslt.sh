#!/usr/bin/env bash
# Profundum Phase 14 T013 (FR-030, FR-032; SC-016; plan section 4.10).
# Applies phase14.xsl to plugins/profundum/resources/editor.uidesc with Saxon-JS (xslt3, repo node_modules),
# restores the file's line endings, then records `git diff --stat` and the editor-baseline lint in the run log.
# The stylesheet refuses a second run (MorphModSource already tagged), so the uidesc is never edited twice.
# Every path is relative to the repo root, so no POSIX path reaches the Windows node / xslt3 binaries.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT"

EVID=specs/profundum-phase14-morph-modulation/evidence
XSL="$EVID/uidesc/phase14.xsl"
UIDESC=plugins/profundum/resources/editor.uidesc
OUT="$EVID/uidesc/editor.uidesc.xslt-out"
LOG="$EVID/run-log/T013.txt"

trap 'rm -f "$OUT"' EXIT

npx --no-install xslt3 -xsl:"$XSL" -s:"$UIDESC" -o:"$OUT"

# Saxon writes LF, runs the XML declaration into the root element and puts every start tag on one line; put back
# the source's layout: declaration on its own line, a start tag the source wraps (the template's) wrapped as before
# when its attributes are unchanged, CRLF if the source used CRLF, one trailing newline.
node -e '
const fs = require("fs");
const [out, target] = process.argv.slice(1);
const src = fs.readFileSync(target, "utf8").replace(/\r\n/g, "\n");
let text = fs.readFileSync(out, "utf8").replace(/\r\n/g, "\n");
text = text.replace(/^(<\?xml[^>]*\?>)\s*</, "$1\n<");
for (const wrapped of src.match(/<[A-Za-z][^>]*\n[^>]*>/g) || []) {
    const oneLine = wrapped.replace(/\s+/g, " ");
    if (text.includes(oneLine)) {
        text = text.replace(oneLine, () => wrapped);
    }
}
text = text.replace(/\n*$/, "\n");
if (fs.readFileSync(target, "utf8").includes("\r\n")) {
    text = text.replace(/\n/g, "\r\n");
}
fs.writeFileSync(target, text, "utf8");
' "$OUT" "$UIDESC"

{
    echo "T013 run-xslt.sh $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "xslt3 -xsl:$XSL -s:$UIDESC"
    echo
    echo "git diff --stat -- $UIDESC"
    git diff --stat -- "$UIDESC"
    echo
    echo "node tools/lint-plugin-editor-baseline.js"
} > "$LOG"
set +e
node tools/lint-plugin-editor-baseline.js >> "$LOG" 2>&1
LINT=$?
set -e
echo "lint exit=$LINT" >> "$LOG"
cat "$LOG"
exit "$LINT"
