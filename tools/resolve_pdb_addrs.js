#!/usr/bin/env node
/**
 * resolve_pdb_addrs.js - Resolve RVA addresses to file:line using PDB debug info
 *
 * Uses llvm-pdbutil to dump line info from a PDB, then parses the output to
 * map RVAs to source locations. This works around the fact that llvm-symbolizer
 * on Windows doesn't support file:line resolution from PDB files (it lacks DIA
 * support and the native PDB reader doesn't provide line info).
 *
 * Usage:
 *   node resolve_pdb_addrs.js <pdb_path> <dll_path> <addr1> [addr2] [addr3] ...
 *
 * Addresses should be RVA offsets (hex, with or without 0x prefix).
 *
 * Example:
 *   node resolve_pdb_addrs.js path/to/Ruinae.pdb path/to/Ruinae.vst3 0xb00e5c 0xaf0b61
 */

const { execSync, execFileSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const readline = require('readline');

// --- Configuration ---
const LLVM_PDBUTIL = 'C:\\Program Files\\LLVM\\bin\\llvm-pdbutil.exe';
const LLVM_READOBJ = 'C:\\Program Files\\LLVM\\bin\\llvm-readobj.exe';
const LLVM_SYMBOLIZER = 'C:\\Program Files\\LLVM\\bin\\llvm-symbolizer.exe';

// --- Parse CLI args ---
const args = process.argv.slice(2);
if (args.length < 3) {
    console.error('Usage: node resolve_pdb_addrs.js <pdb_path> <dll_path> <addr1> [addr2] ...');
    console.error('  Addresses are RVA hex values (with or without 0x prefix)');
    process.exit(1);
}

const pdbPath = args[0];
const dllPath = args[1];
const targetRVAs = args.slice(2).map(a => {
    const v = parseInt(a, 16);
    if (isNaN(v)) {
        console.error(`Invalid hex address: ${a}`);
        process.exit(1);
    }
    return v;
});

// --- Step 1: Get section layout from DLL ---
function getSectionHeaders(dllPath) {
    console.error('[1/4] Reading section headers from DLL...');
    const output = execFileSync(LLVM_READOBJ, [
        '--section-headers', dllPath
    ], { encoding: 'utf8', maxBuffer: 10 * 1024 * 1024 });

    const sections = [];
    // llvm-readobj format:
    //   Section {
    //     Number: 1
    //     Name: .text (...)
    //     VirtualSize: 0x99931F
    //     VirtualAddress: 0x49C000
    const sectionBlocks = output.split(/Section \{/g);
    for (const block of sectionBlocks) {
        const numMatch = block.match(/Number:\s*(\d+)/);
        const nameMatch = block.match(/Name:\s*(\S+)/);
        const vsizeMatch = block.match(/VirtualSize:\s*0x([0-9A-Fa-f]+)/);
        const vaddrMatch = block.match(/VirtualAddress:\s*0x([0-9A-Fa-f]+)/);
        if (numMatch && nameMatch && vsizeMatch && vaddrMatch) {
            sections.push({
                index: parseInt(numMatch[1]),
                name: nameMatch[1],
                virtualSize: parseInt(vsizeMatch[1], 16),
                virtualAddress: parseInt(vaddrMatch[1], 16)
            });
        }
    }
    return sections;
}

// --- Step 2: Get symbol names via llvm-symbolizer (for context) ---
function getSymbolNames(dllPath, rvas) {
    console.error('[2/4] Resolving symbol names via llvm-symbolizer...');
    const results = [];
    for (const rva of rvas) {
        const input = '0x' + rva.toString(16) + '\n';
        try {
            const output = execFileSync(LLVM_SYMBOLIZER, [
                '--obj=' + dllPath, '--relative-address', '--demangle'
            ], { input, encoding: 'utf8', maxBuffer: 10 * 1024 * 1024 });
            const firstLine = output.trim().split('\n')[0] || '??';
            results.push(firstLine.trim());
        } catch {
            results.push('??');
        }
    }
    return results;
}

// --- Step 3: Parse llvm-pdbutil line dump ---
function parsePdbLines(pdbPath, sections, targetRVAs) {
    console.error('[3/4] Dumping and parsing PDB line info (this may take a moment)...');

    // Build segment-to-VA mapping (PDB segments are 1-based)
    const segmentBaseVA = {};
    for (const s of sections) {
        segmentBaseVA[s.index] = s.virtualAddress;
    }

    // We'll build an array of {rva, endRva, line, file} entries for binary search
    // But since we only need specific addresses, we can be smarter:
    // collect all line entries and find the best match for each target RVA.

    const targetSet = new Set(targetRVAs);

    // For each target RVA, track the best match: largest startRVA <= targetRVA
    const bestMatch = {}; // rva -> { file, line, startRVA, endRVA }
    for (const rva of targetRVAs) {
        bestMatch[rva] = null;
    }

    // Parse the dump output line by line
    const output = execFileSync(LLVM_PDBUTIL, [
        'dump', '-l', pdbPath
    ], { encoding: 'utf8', maxBuffer: 100 * 1024 * 1024 });

    const lines = output.split('\n');
    let currentFile = null;

    // Regex patterns
    const filePattern = /^(\S.+?)(?:\s+\(SHA-256:.*\))?$/;
    const rangePattern = /^\s+(\d{4}):([0-9A-Fa-f]+)-([0-9A-Fa-f]+),\s*line\/addr entries\s*=\s*(\d+)/;
    const lineEntryPattern = /(\d+)\s+([0-9A-Fa-f]+)/g;
    const modPattern = /^Mod \d{4}/;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];

        // Skip empty lines and the header
        if (!line.trim() || line.startsWith('===') || line.startsWith('  ')) {
            // Could be a line entry or range line (starts with spaces)
            // Check for range line
            const rangeMatch = line.match(rangePattern);
            if (rangeMatch) {
                const segment = parseInt(rangeMatch[1]);
                const startOffset = parseInt(rangeMatch[2], 16);
                const endOffset = parseInt(rangeMatch[3], 16);
                const baseVA = segmentBaseVA[segment];
                if (baseVA === undefined) continue;

                const startRVA = baseVA + startOffset;
                const endRVA = baseVA + endOffset;

                // Quick check: does any target fall in this range?
                let hasTarget = false;
                for (const rva of targetRVAs) {
                    if (rva >= startRVA && rva <= endRVA) {
                        hasTarget = true;
                        break;
                    }
                }
                if (!hasTarget) continue;

                // Parse the next line(s) for line/addr entries
                const nextLine = lines[i + 1];
                if (!nextLine) continue;

                // Collect all entries from continuation lines
                let entryText = '';
                for (let j = i + 1; j < lines.length; j++) {
                    const el = lines[j];
                    if (!el || !el.match(/^\s+\d/)) break;
                    entryText += ' ' + el;
                }

                // Parse all line/addr entries
                const entries = [];
                let entryMatch;
                const re = /(\d+)\s+([0-9A-Fa-f]+)/g;
                while ((entryMatch = re.exec(entryText)) !== null) {
                    entries.push({
                        line: parseInt(entryMatch[1]),
                        offset: parseInt(entryMatch[2], 16)
                    });
                }

                // For each target RVA in this range, find the best line entry
                for (const rva of targetRVAs) {
                    if (rva < startRVA || rva > endRVA) continue;

                    const targetOffset = rva - baseVA;
                    // Find the entry with largest offset <= targetOffset
                    let bestEntry = null;
                    for (const e of entries) {
                        if (e.offset <= targetOffset) {
                            if (!bestEntry || e.offset > bestEntry.offset) {
                                bestEntry = e;
                            }
                        }
                    }
                    if (bestEntry && currentFile) {
                        const entryRVA = baseVA + bestEntry.offset;
                        const prev = bestMatch[rva];
                        if (!prev || entryRVA > prev.startRVA) {
                            bestMatch[rva] = {
                                file: currentFile,
                                line: bestEntry.line,
                                startRVA: entryRVA,
                                endRVA: endRVA
                            };
                        }
                    }
                }
                continue;
            }

            continue;
        }

        // Check for module header
        if (modPattern.test(line)) {
            continue;
        }

        // Check for file path (non-indented line that's a path)
        const trimmed = line.trim();
        if (trimmed && !trimmed.startsWith('Mod ') && !trimmed.startsWith('===') && !trimmed.startsWith('Lines')) {
            // Extract file path (remove SHA-256 suffix if present)
            const shaIdx = trimmed.indexOf(' (SHA-256:');
            currentFile = shaIdx >= 0 ? trimmed.substring(0, shaIdx) : trimmed;
        }
    }

    return bestMatch;
}

// --- Main ---
function main() {
    // Step 1: Section headers
    const sections = getSectionHeaders(dllPath);
    console.error(`  Found ${sections.length} sections:`);
    for (const s of sections) {
        console.error(`    #${s.index} ${s.name.padEnd(10)} VA=0x${s.virtualAddress.toString(16).padStart(8, '0')} Size=0x${s.virtualSize.toString(16)}`);
    }

    // Step 2: Symbol names
    const symbolNames = getSymbolNames(dllPath, targetRVAs);

    // Step 3: Parse PDB line info
    const matches = parsePdbLines(pdbPath, sections, targetRVAs);

    // Step 4: Output results
    console.error('[4/4] Results:\n');

    console.log('='.repeat(120));
    console.log('  RVA Address Resolution Results');
    console.log('='.repeat(120));
    console.log('');

    for (let i = 0; i < targetRVAs.length; i++) {
        const rva = targetRVAs[i];
        const rvaHex = '0x' + rva.toString(16);
        const symbol = symbolNames[i] || '??';
        const match = matches[rva];

        console.log(`[${(i + 1).toString().padStart(2)}] RVA ${rvaHex}`);
        console.log(`     Symbol:  ${symbol}`);
        if (match) {
            console.log(`     Source:  ${match.file}:${match.line}`);
            console.log(`     Match:   offset 0x${match.startRVA.toString(16)} (delta: +${rva - match.startRVA} bytes)`);
        } else {
            console.log(`     Source:  <no line info found>`);
        }
        console.log('');
    }
}

main();
