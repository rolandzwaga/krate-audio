#!/usr/bin/env node
// One-shot converter: Membrum's early .memkit factory presets wrap nothing —
// they are the raw serialised KitSnapshot blob that Membrum::State::writeKitBlob
// writes. The shared PresetBrowserView scans only .vstpreset files, so these
// never showed up. This script rewraps each .memkit as a proper VST3 preset
// file (same component-state bytes, standard VST3 header + chunk list + XML
// metadata).
//
// Usage:  node tools/convert-memkit.js [--delete]
//   --delete  remove the source .memkit files after a successful conversion
//
// Format reference: public.sdk/source/vst/vstpresetfile.cpp and
//                   plugins/shared/src/preset/preset_manager.cpp (savePreset).

const fs = require("fs");
const path = require("path");

const RESOURCES_ROOT = path.join(
    __dirname, "..", "plugins", "membrum", "resources", "presets");

// Membrum processor UID as a 32-character uppercase hex string, matching
// FUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136) on Windows
// (COM_COMPATIBLE). Verified against an existing valid .vstpreset file.
const CLASS_ID = "4D656D6272756D50726F6331000001 36".replace(/\s/g, "");

if (CLASS_ID.length !== 32) {
    throw new Error(`CLASS_ID length ${CLASS_ID.length}, expected 32`);
}

function metaInfoXml({ name, subcategory }) {
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n' +
        '<MetaInfo>\n' +
        '  <Attr id="MediaType" value="VstPreset" type="string"/>\n' +
        '  <Attr id="PlugInName" value="Membrum/Kits" type="string"/>\n' +
        '  <Attr id="PlugInCategory" value="Kit Presets" type="string"/>\n' +
        `  <Attr id="Name" value="${name}" type="string"/>\n` +
        `  <Attr id="MusicalCategory" value="${subcategory}" type="string"/>\n` +
        `  <Attr id="MusicalInstrument" value="${subcategory}" type="string"/>\n` +
        '</MetaInfo>\n'
    );
}

function writePresetFile(outputPath, componentState, xml) {
    const HEADER_SIZE = 4 + 4 + 32 + 8;        // 'VST3' + version + classID + listOffset
    const ENTRY_SIZE = 4 + 8 + 8;              // chunkID + offset + size
    const ENTRY_COUNT = 2;                     // Comp + Info
    const LIST_SIZE = 4 + 4 + ENTRY_COUNT * ENTRY_SIZE;

    const componentOffset = HEADER_SIZE;
    const componentSize = componentState.length;
    const infoOffset = componentOffset + componentSize;
    const infoBytes = Buffer.from(xml, "utf8");
    const infoSize = infoBytes.length;
    const listOffset = infoOffset + infoSize;

    const fileSize = listOffset + LIST_SIZE;
    const out = Buffer.alloc(fileSize);

    // Header
    out.write("VST3", 0, 4, "ascii");
    out.writeInt32LE(1, 4);                                          // format version
    out.write(CLASS_ID, 8, 32, "ascii");                             // classID string
    out.writeBigInt64LE(BigInt(listOffset), 40);                     // listOffset

    // Body
    componentState.copy(out, componentOffset);
    infoBytes.copy(out, infoOffset);

    // Chunk list
    let p = listOffset;
    out.write("List", p, 4, "ascii");                                p += 4;
    out.writeInt32LE(ENTRY_COUNT, p);                                p += 4;

    out.write("Comp", p, 4, "ascii");                                p += 4;
    out.writeBigInt64LE(BigInt(componentOffset), p);                 p += 8;
    out.writeBigInt64LE(BigInt(componentSize), p);                   p += 8;

    out.write("Info", p, 4, "ascii");                                p += 4;
    out.writeBigInt64LE(BigInt(infoOffset), p);                      p += 8;
    out.writeBigInt64LE(BigInt(infoSize), p);                        p += 8;

    if (p !== fileSize) {
        throw new Error(`write cursor ${p} != file size ${fileSize}`);
    }

    fs.writeFileSync(outputPath, out);
}

function findMemkits(root) {
    const results = [];
    function walk(dir) {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) {
                walk(full);
            } else if (entry.isFile() && entry.name.endsWith(".memkit")) {
                results.push(full);
            }
        }
    }
    walk(root);
    return results;
}

function main() {
    const deleteSource = process.argv.includes("--delete");

    const memkits = findMemkits(RESOURCES_ROOT);
    if (memkits.length === 0) {
        console.log(`no .memkit files under ${RESOURCES_ROOT}`);
        return;
    }

    for (const src of memkits) {
        const dir = path.dirname(src);
        const baseName = path.basename(src, ".memkit");
        const subcategory = path.basename(dir);
        const dest = path.join(dir, `${baseName}.vstpreset`);

        const componentState = fs.readFileSync(src);
        const xml = metaInfoXml({ name: baseName, subcategory });

        writePresetFile(dest, componentState, xml);
        const stats = fs.statSync(dest);
        console.log(
            `converted ${path.relative(RESOURCES_ROOT, src)} -> ` +
            `${path.relative(RESOURCES_ROOT, dest)} (${stats.size} bytes)`);

        if (deleteSource) {
            fs.unlinkSync(src);
            console.log(`  removed source`);
        }
    }
}

main();
