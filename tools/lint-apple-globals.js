#!/usr/bin/env node
// lint-apple-globals.js
// =============================================================================
// Flags identifiers that compile everywhere except macOS.
//
// Two macOS-only failure classes cost one CI round each on 2026-10-09 (run 37910949575 and its re-run):
//
//   1. A PLATFORM MACRO used as an identifier. vstgui/lib/vstguibase.h does `#define MAC 1` on macOS
//      (WINDOWS / LINUX on the others), so `enum class Route { ..., MAC, ... }` in a TU that includes
//      VSTGUI became `Route::1` on macOS only ("expected identifier", param_routes.h:28).
//   2. A NAME AT GLOBAL OR ANONYMOUS-NAMESPACE SCOPE that MacTypes.h also declares. Any TU that includes
//      VSTGUI on macOS sees MacTypes.h's global `Rect`, `Point`, `Style`, `Size`, `Fixed`, `Boolean`, ...;
//      an anonymous-namespace `struct Rect` is found by the same unqualified lookup, so every use is
//      "reference to 'Rect' is ambiguous" (editor_layout_test.cpp:1047). A name inside a NAMED namespace
//      is safe (inner-scope lookup wins) unless a `using namespace` drags it to global level.
//
// Windows and Linux never see either, so the per-push lanes there stay green; this lint runs on the Linux
// lint job and fails the push before a macOS build is spent on it.
//
// Usage: node tools/lint-apple-globals.js [dir-or-file ...]   (default: plugins/)
// Exit 1 on any hit. Heuristic, line-based, column-0 declarations + enumerators + macro tokens; comments
// and string literals are stripped first.
'use strict';
const fs = require('fs');
const path = require('path');

// MacTypes.h / CoreServices global type and constant names (unprefixed), as shipped in the macOS SDK.
const kAppleGlobals = new Set(`UInt8 SInt8 UInt16 SInt16 UInt32 SInt32 UInt64 SInt64 wide UnsignedWide Fixed FixedPtr Fract FractPtr
UnsignedFixed UnsignedFixedPtr ShortFixed ShortFixedPtr Float32 Float64 Float80 Float96 Float32Point Ptr Handle Size
OSErr OSStatus LogicalAddress ConstLogicalAddress PhysicalAddress BytePtr ByteCount ByteOffset Duration AbsoluteTime
OptionBits ItemCount PBVersion ScriptCode LangCode RegionCode FourCharCode OSType ResType OSTypePtr ResTypePtr Boolean
ProcPtr UniversalProcPtr ProcHandle UniversalProcHandle SRefCon URefCon Str255 Str63 Str32 Str31 Str27 Str15 StringPtr
StringHandle ConstStringPtr ConstStr255Param ConstStr63Param ConstStr32Param ConstStr31Param ConstStr27Param
ConstStr15Param Byte SignedByte WidePtr UnsignedWidePtr UniChar UniCharPtr UniCharCount UniCharCountPtr UTF32Char
UTF16Char UTF8Char UnicodeScalarValue Point Rect FixedPoint FixedRect ProcessSerialNumber Style StyleParameter
StyleField TimeValue TimeScale TimeBase TimeRecord NumVersion NumVersionVariant VersRec VersRecPtr VersRecHndl ResID
normal bold italic underline outline shadow condense extend noErr kNilOptions kInvalidID kVariableLengthArray
kUnknownType Debugger DebugStr SysBreak SysBreakStr SysBreakFunc`.split(/\s+/).filter(Boolean));

// Macros that VSTGUI (vstgui/lib/vstguibase.h) or the build itself define on at least one platform; an
// identifier with one of these names is rewritten by the preprocessor wherever the macro is in force.
const kPlatformMacros = new Set(['MAC', 'WINDOWS', 'LINUX', 'MAC_COCOA', 'MAC_CARBON', 'NOMINMAX', 'UNICODE', 'DEBUG', 'RELEASE']);

const args = process.argv.slice(2);
const roots = args.length ? args : ['plugins'];
const files = [];
function walk(d) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const p = path.join(d, e.name);
        if (e.isDirectory()) walk(p);
        else if (/\.(h|hpp|cpp|mm)$/.test(e.name)) files.push(p);
    }
}
for (const r of roots) (fs.statSync(r).isDirectory() ? walk(r) : files.push(r));

const stripNoise = (raw) => raw.replace(/\/\/.*$/, '').replace(/"(?:[^"\\]|\\.)*"/g, '""').replace(/'(?:[^'\\]|\\.)*'/g, "''");

let hits = 0;
const report = (f, i, what, raw) => { hits++; console.log(`${f}:${i + 1}: ${what}: ${raw.trim().slice(0, 100)}`); };

for (const f of files) {
    const lines = fs.readFileSync(f, 'utf8').split(/\r?\n/);
    let named = 0;        // depth of named namespaces (names there are safe from ::Rect)
    let inEnum = false;   // inside a column-0 / column-4 enum body: enumerators are namespace-scope names
    let block = false;    // inside a /* */ block comment
    lines.forEach((raw, i) => {
        let line = raw;
        if (block) { const e = line.indexOf('*/'); if (e < 0) return; line = line.slice(e + 2); block = false; }
        const b = line.indexOf('/*');
        if (b >= 0) { const e = line.indexOf('*/', b + 2); line = e >= 0 ? line.slice(0, b) + line.slice(e + 2) : (block = true, line.slice(0, b)); }
        line = stripNoise(line);
        const isPreprocessor = /^\s*#/.test(line);

        // (1) platform macros used as identifiers, any scope (skip preprocessor lines: #ifdef MAC is the
        //     legitimate use, and `#define` lines are the definitions themselves)
        if (!isPreprocessor) {
            for (const m of line.matchAll(/[A-Za-z_]\w*/g)) {
                if (kPlatformMacros.has(m[0])) report(f, i, `'${m[0]}' is a VSTGUI/build platform macro on at least one OS`, raw);
            }
        }

        // scope tracking (column-0 namespaces only; good enough for this repository's layout)
        if (/^namespace\s+[\w:]+\s*\{/.test(line)) named++;
        else if (/^\}\s*\/\/\s*namespace\s+\w/.test(raw) && named > 0) named--;
        if (/^(?:    )?enum(?:\s+class)?\s+\w+[^{;]*\{/.test(line) && !/\}/.test(line)) inEnum = !/enum\s+class/.test(line);  // unscoped only
        else if (inEnum && /^\s*\};/.test(line)) inEnum = false;
        if (named > 0) return;

        // (2) global / anonymous-namespace declarations named like a MacTypes global
        const decl = [];
        let m;
        if ((m = /^(?:struct|class|enum class|enum|using)\s+(\w+)\b/.exec(line))) decl.push(m[1]);
        if ((m = /^typedef\b[^;]*?\b(\w+)\s*;/.exec(line))) decl.push(m[1]);
        if ((m = /^(?:\[\[\w+\]\]\s*)?(?:(?:static|inline|constexpr|const)\s+)*[\w:<>,\s*&]+?\s+(\w+)\s*\(/.exec(line))
            && !/^(?:if|for|while|switch|return|else|case|TEST_CASE|SECTION|REQUIRE|CHECK|namespace)\b/.test(line)) decl.push(m[1]);
        if ((m = /^(?:(?:static|inline|constexpr|const)\s+)+[\w:<>,\s*&]+?\s+(\w+)\s*(?:=|\{|;)/.exec(line))) decl.push(m[1]);
        if (inEnum && (m = /^\s{4}(\w+)\s*(?:=|,|$)/.exec(line))) decl.push(m[1]);
        for (const d of decl) {
            if (kAppleGlobals.has(d)) report(f, i, `'${d}' at global/anonymous-namespace scope is also a MacTypes.h global on macOS (ambiguous in any TU that includes VSTGUI)`, raw);
        }
    });
}

if (hits === 0) {
    console.log(`lint-apple-globals: OK — ${files.length} files, no macOS-only name collisions`);
    process.exit(0);
}
console.log(`lint-apple-globals: ${hits} collision(s) in ${files.length} files — rename the identifier (e.g. Rect -> LayoutRect, MAC -> Macro)`);
process.exit(1);
