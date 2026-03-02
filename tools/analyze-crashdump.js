#!/usr/bin/env node
// ==============================================================================
// Minidump Analyzer - Extract crash info from Windows .dmp files
// ==============================================================================
// Parses MDMP format to find exception record, crashing thread, and module list.
// Cross-references RIP/instruction pointer against loaded modules to identify
// which DLL/VST3 crashed and at what offset.
//
// Usage: node analyze-crashdump.js <path-to-dmp>
// ==============================================================================

const fs = require('fs');
const path = require('path');

const dmpPath = process.argv[2];
if (!dmpPath) {
    console.error('Usage: node analyze-crashdump.js <path-to-dmp>');
    process.exit(1);
}

const buf = fs.readFileSync(dmpPath);

// MDMP header
const sig = buf.toString('ascii', 0, 4);
if (sig !== 'MDMP') {
    console.error('Not a valid minidump file');
    process.exit(1);
}

const version = buf.readUInt16LE(4);
const numStreams = buf.readUInt32LE(8);
const streamDirRva = buf.readUInt32LE(12);

console.log(`=== Minidump Header ===`);
console.log(`Signature: ${sig}, Version: ${version}`);
console.log(`Streams: ${numStreams}, Directory RVA: 0x${streamDirRva.toString(16)}`);
console.log();

// Stream types we care about
const STREAM_TYPES = {
    3: 'ThreadListStream',
    4: 'ModuleListStream',
    6: 'ExceptionStream',
    7: 'SystemInfoStream',
    // 9: 'ThreadInfoListStream',
};

// Parse stream directory
const streams = {};
for (let i = 0; i < numStreams; i++) {
    const off = streamDirRva + i * 12;
    const streamType = buf.readUInt32LE(off);
    const dataSize = buf.readUInt32LE(off + 4);
    const dataRva = buf.readUInt32LE(off + 8);
    if (STREAM_TYPES[streamType]) {
        streams[streamType] = { type: STREAM_TYPES[streamType], dataSize, dataRva };
    }
}

// Parse System Info
if (streams[7]) {
    const rva = streams[7].dataRva;
    const procArch = buf.readUInt16LE(rva);
    const archNames = { 0: 'x86', 9: 'x64', 5: 'ARM', 12: 'ARM64' };
    console.log(`=== System Info ===`);
    console.log(`Architecture: ${archNames[procArch] || procArch}`);
    console.log();
}

// Parse Module List
const modules = [];
if (streams[4]) {
    const rva = streams[4].dataRva;
    const numModules = buf.readUInt32LE(rva);
    console.log(`=== Modules (${numModules} total) ===`);

    for (let i = 0; i < numModules; i++) {
        const mOff = rva + 4 + i * 108; // MINIDUMP_MODULE is 108 bytes
        const baseAddr = buf.readBigUInt64LE(mOff);
        const sizeOfImage = buf.readUInt32LE(mOff + 8);
        // MINIDUMP_MODULE layout: BaseOfImage(8) + SizeOfImage(4) + CheckSum(4) + TimeDateStamp(4) + ModuleNameRva(4)
        const moduleNameRva = buf.readUInt32LE(mOff + 20);

        // Read module name (MINIDUMP_STRING: uint32 length + UTF-16LE)
        let moduleName = '';
        if (moduleNameRva > 0 && moduleNameRva < buf.length - 4) {
            const nameLen = buf.readUInt32LE(moduleNameRva);
            if (nameLen > 0 && nameLen < 1024) {
                moduleName = buf.toString('utf16le', moduleNameRva + 4, moduleNameRva + 4 + nameLen);
            }
        }

        modules.push({
            baseAddr,
            endAddr: baseAddr + BigInt(sizeOfImage),
            sizeOfImage,
            name: moduleName,
        });
    }

    // Sort by base address
    modules.sort((a, b) => (a.baseAddr < b.baseAddr ? -1 : 1));

    // Print modules related to our plugin or key DLLs
    const interestingModules = modules.filter(m => {
        const lower = m.name.toLowerCase();
        return lower.includes('ruinae') || lower.includes('iterum') ||
               lower.includes('disrumpo') || lower.includes('vstgui') ||
               lower.includes('reaper') || lower.includes('ntdll') ||
               lower.includes('kernelbase') || lower.includes('ucrtbase') ||
               lower.includes('vcruntime') || lower.includes('msvcp');
    });

    for (const m of interestingModules) {
        console.log(`  0x${m.baseAddr.toString(16).padStart(16, '0')}-0x${m.endAddr.toString(16).padStart(16, '0')} ${path.basename(m.name)} (${(m.sizeOfImage / 1024).toFixed(0)}KB)`);
    }
    console.log();
}

// Helper: find module for an address
function findModule(addr) {
    const bigAddr = typeof addr === 'bigint' ? addr : BigInt(addr);
    for (const m of modules) {
        if (bigAddr >= m.baseAddr && bigAddr < m.endAddr) {
            return m;
        }
    }
    return null;
}

function formatAddr(addr) {
    const bigAddr = typeof addr === 'bigint' ? addr : BigInt(addr);
    const mod = findModule(bigAddr);
    if (mod) {
        const offset = bigAddr - mod.baseAddr;
        return `0x${bigAddr.toString(16)} (${path.basename(mod.name)}+0x${offset.toString(16)})`;
    }
    return `0x${bigAddr.toString(16)}`;
}

// Parse Exception Stream
let exceptionThreadId = 0;
let exceptionAddr = 0n;
if (streams[6]) {
    const rva = streams[6].dataRva;
    exceptionThreadId = buf.readUInt32LE(rva);
    // __alignment at rva+4
    // MINIDUMP_EXCEPTION at rva+8
    const exRva = rva + 8;
    const exceptionCode = buf.readUInt32LE(exRva);
    const exceptionFlags = buf.readUInt32LE(exRva + 4);
    exceptionAddr = buf.readBigUInt64LE(exRva + 16);
    const numParams = buf.readUInt32LE(exRva + 24);

    const exCodeNames = {
        0xC0000005: 'ACCESS_VIOLATION',
        0xC0000094: 'INTEGER_DIVIDE_BY_ZERO',
        0xC00000FD: 'STACK_OVERFLOW',
        0xC0000409: 'STACK_BUFFER_OVERRUN',
        0x80000003: 'BREAKPOINT',
        0xE06D7363: 'C++ EXCEPTION (throw)',
        0xC0000374: 'HEAP_CORRUPTION',
    };

    console.log(`=== Exception ===`);
    console.log(`Thread ID: ${exceptionThreadId} (0x${exceptionThreadId.toString(16)})`);
    console.log(`Code: 0x${exceptionCode.toString(16).padStart(8, '0')} (${exCodeNames[exceptionCode] || 'unknown'})`);
    console.log(`Address: ${formatAddr(exceptionAddr)}`);

    if (exceptionCode === 0xC0000005 && numParams >= 2) {
        const accessType = buf.readBigUInt64LE(exRva + 28);
        const targetAddr = buf.readBigUInt64LE(exRva + 36);
        const accessName = accessType === 0n ? 'READ' : accessType === 1n ? 'WRITE' : 'EXECUTE';
        console.log(`Access Violation: ${accessName} at 0x${targetAddr.toString(16)}`);
    }

    // Context record
    const ctxRva = rva + 8 + 152; // After MINIDUMP_EXCEPTION (152 bytes)
    const ctxDataSize = buf.readUInt32LE(ctxRva);
    const ctxDataRva = buf.readUInt32LE(ctxRva + 4);

    if (ctxDataRva > 0 && ctxDataRva + 100 < buf.length) {
        // CONTEXT structure for x64 - RIP is at offset 248 (0xF8)
        const contextFlags = buf.readUInt32LE(ctxDataRva + 48);
        if (ctxDataRva + 0xF8 + 8 <= buf.length) {
            const rip = buf.readBigUInt64LE(ctxDataRva + 0xF8);
            const rsp = buf.readBigUInt64LE(ctxDataRva + 0x98);
            const rbp = buf.readBigUInt64LE(ctxDataRva + 0xA0); // Not standard but close
            console.log(`RIP: ${formatAddr(rip)}`);
            console.log(`RSP: 0x${rsp.toString(16)}`);
        }
    }
    console.log();
}

// Parse Thread List
if (streams[3]) {
    const rva = streams[3].dataRva;
    const numThreads = buf.readUInt32LE(rva);
    console.log(`=== Threads (${numThreads} total) ===`);

    // MINIDUMP_THREAD layout (48 bytes):
    //   0: ThreadId (4)
    //   4: SuspendCount (4)
    //   8: PriorityClass (4)
    //  12: Priority (4)
    //  16: Teb (8)
    //  24: Stack.StartOfMemoryRange (8)
    //  32: Stack.Memory.DataSize (4)
    //  36: Stack.Memory.Rva (4)
    //  40: ThreadContext.DataSize (4)
    //  44: ThreadContext.Rva (4)
    for (let i = 0; i < numThreads; i++) {
        const tOff = rva + 4 + i * 48;
        const threadId = buf.readUInt32LE(tOff);
        const stackStart = buf.readBigUInt64LE(tOff + 24);
        const stackDataSize = buf.readUInt32LE(tOff + 32);
        const stackDataRva = buf.readUInt32LE(tOff + 36);
        const ctxSize = buf.readUInt32LE(tOff + 40);
        const ctxRva = buf.readUInt32LE(tOff + 44);

        const isCrashThread = threadId === exceptionThreadId;

        if (isCrashThread) {
            console.log(`\n>>> CRASHING THREAD ${threadId} (0x${threadId.toString(16)}) <<<`);

            // Read RIP from context
            if (ctxRva > 0 && ctxRva + 0xF8 + 8 <= buf.length) {
                const rip = buf.readBigUInt64LE(ctxRva + 0xF8);
                const rsp = buf.readBigUInt64LE(ctxRva + 0x98);
                console.log(`  RIP: ${formatAddr(rip)}`);
                console.log(`  RSP: 0x${rsp.toString(16)}`);

                // Walk the stack memory looking for return addresses in known modules
                console.log(`  Stack base: 0x${stackStart.toString(16)}, size: ${stackDataSize}, data RVA: 0x${stackDataRva.toString(16)}`);
                console.log();
                console.log(`  === Stack Return Address Scan ===`);

                let count = 0;
                if (stackDataRva > 0 && stackDataRva + stackDataSize <= buf.length) {
                    // Scan stack memory for potential return addresses (heuristic)
                    for (let sOff = 0; sOff + 8 <= stackDataSize && count < 60; sOff += 8) {
                        const val = buf.readBigUInt64LE(stackDataRva + sOff);
                        const mod = findModule(val);
                        if (mod) {
                            const offset = val - mod.baseAddr;
                            const basename = path.basename(mod.name);
                            const addrOnStack = stackStart + BigInt(sOff);
                            console.log(`  [0x${addrOnStack.toString(16)}] ${basename}+0x${offset.toString(16)}`);
                            count++;
                        }
                    }
                } else {
                    console.log(`  (stack memory not in dump or RVA invalid)`);
                }
            }
            console.log();
        }
    }
}

// Print all modules with Ruinae in the name for PDB matching
console.log(`=== Ruinae Module Details ===`);
for (const m of modules) {
    if (m.name.toLowerCase().includes('ruinae')) {
        console.log(`  Path: ${m.name}`);
        console.log(`  Base: 0x${m.baseAddr.toString(16)}`);
        console.log(`  Size: 0x${m.sizeOfImage.toString(16)} (${(m.sizeOfImage / 1024).toFixed(0)}KB)`);
    }
}
