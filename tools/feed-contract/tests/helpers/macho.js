// Synthetic thin and fat Mach-O buffers for the release-facts tests (spec SC-004, SC-024; plan §4.12.1).
//
// WHY: tools/release-facts.js reads the architecture set and the minimum macOS from the load commands of every
// Mach-O in an expanded pkg. The tests must prove that reader on Windows, where no Apple toolchain exists, so this
// helper writes the byte layouts directly from Apple's headers:
//   loader.h  https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/loader.h
//             (mach_header / mach_header_64, load_command, build_version_command, version_min_command)
//   fat.h     https://raw.githubusercontent.com/apple-oss-distributions/cctools/main/include/mach-o/fat.h
//             (fat_header, fat_arch, fat_arch_64; "always written and read ... in big-endian order")
//   machine.h https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/mach/machine.h
//             (CPU_ARCH_ABI64, CPU_TYPE_*, CPU_SUBTYPE_*)
// A thin file's fields are written little-endian whatever its magic, so a "big-endian" or 32-bit file carries the
// on-disk magic bytes the reader must reject, nothing more. Pure: builds Buffers only, no fs, no child process.
'use strict';

// loader.h
const MH_MAGIC = 0xfeedface;
const MH_CIGAM = 0xcefaedfe;
const MH_MAGIC_64 = 0xfeedfacf;
const MH_CIGAM_64 = 0xcffaedfe;
const MH_BUNDLE = 0x8;
const LC_VERSION_MIN_MACOSX = 0x24;
const LC_BUILD_VERSION = 0x32;
const PLATFORM_MACOS = 1;
const PLATFORM_IOS = 2;
// fat.h
const FAT_MAGIC = 0xcafebabe;
const FAT_MAGIC_64 = 0xcafebabf;
// machine.h
const CPU_ARCH_ABI64 = 0x01000000;
const CPU_TYPE_X86 = 7;
const CPU_TYPE_I386 = CPU_TYPE_X86;
const CPU_TYPE_X86_64 = (CPU_TYPE_X86 | CPU_ARCH_ABI64) >>> 0;
const CPU_TYPE_ARM = 12;
const CPU_TYPE_ARM64 = (CPU_TYPE_ARM | CPU_ARCH_ABI64) >>> 0;
const CPU_SUBTYPE_ARM64_ALL = 0;
const CPU_SUBTYPE_ARM64_V8 = 1;
const CPU_SUBTYPE_ARM64E = 2;
const CPU_SUBTYPE_X86_64_ALL = 3;
const CPU_SUBTYPE_I386_ALL = 3;

const FAT_ALIGN_LOG2 = 12; // slices aligned to 4096, as lipo writes them
const align = (n, a) => Math.ceil(n / a) * a;

/** One load command: cmd, cmdsize (little-endian), the body, zero-padded to a multiple of 8. */
function loadCommand({ cmd, body }) {
  const size = align(8 + body.length, 8);
  const b = Buffer.alloc(size);
  b.writeUInt32LE(cmd >>> 0, 0);
  b.writeUInt32LE(size, 4);
  body.copy(b, 8);
  return b;
}

/** build_version_command body: platform, minos, sdk, ntools (0). */
function buildVersion(minos, platform = PLATFORM_MACOS) {
  const body = Buffer.alloc(16);
  body.writeUInt32LE(platform >>> 0, 0);
  body.writeUInt32LE(minos >>> 0, 4);
  body.writeUInt32LE(minos >>> 0, 8);
  return { cmd: LC_BUILD_VERSION, body };
}

/** version_min_command body: version, sdk. */
function versionMin(version) {
  const body = Buffer.alloc(8);
  body.writeUInt32LE(version >>> 0, 0);
  body.writeUInt32LE(version >>> 0, 4);
  return { cmd: LC_VERSION_MIN_MACOSX, body };
}

/** A thin Mach-O: header (28 bytes for a 32-bit magic, 32 for a 64-bit one) and the load commands. */
function thin({ cputype, cpusubtype = 0, commands = [], magic = MH_MAGIC_64 }) {
  const is32 = magic === MH_MAGIC || magic === MH_CIGAM;
  const cmds = commands.map(loadCommand);
  const sizeofcmds = cmds.reduce((n, c) => n + c.length, 0);
  const header = Buffer.alloc(is32 ? 28 : 32);
  header.writeUInt32LE(magic >>> 0, 0);
  header.writeUInt32LE(cputype >>> 0, 4);
  header.writeUInt32LE(cpusubtype >>> 0, 8);
  header.writeUInt32LE(MH_BUNDLE, 12);
  header.writeUInt32LE(cmds.length, 16);
  header.writeUInt32LE(sizeofcmds, 20);
  return Buffer.concat([header, ...cmds]);
}

/** A fat file over thin slices; each entry's cputype / cpusubtype is copied from its slice's header. */
function fat(slices, { wide = false } = {}) {
  const entrySize = wide ? 32 : 20;
  const header = Buffer.alloc(8 + slices.length * entrySize);
  header.writeUInt32BE(wide ? FAT_MAGIC_64 : FAT_MAGIC, 0);
  header.writeUInt32BE(slices.length, 4);
  const parts = [header];
  let offset = align(header.length, 1 << FAT_ALIGN_LOG2);
  let end = header.length;
  slices.forEach((slice, i) => {
    const at = 8 + i * entrySize;
    header.writeUInt32BE(slice.readUInt32LE(4), at);
    header.writeUInt32BE(slice.readUInt32LE(8), at + 4);
    if (wide) {
      header.writeBigUInt64BE(BigInt(offset), at + 8);
      header.writeBigUInt64BE(BigInt(slice.length), at + 16);
      header.writeUInt32BE(FAT_ALIGN_LOG2, at + 24);
    } else {
      header.writeUInt32BE(offset, at + 8);
      header.writeUInt32BE(slice.length, at + 12);
      header.writeUInt32BE(FAT_ALIGN_LOG2, at + 16);
    }
    parts.push(Buffer.alloc(offset - end), slice);
    end = offset + slice.length;
    offset = align(end, 1 << FAT_ALIGN_LOG2);
  });
  return Buffer.concat(parts);
}

module.exports = {
  MH_MAGIC,
  MH_CIGAM,
  MH_MAGIC_64,
  MH_CIGAM_64,
  LC_VERSION_MIN_MACOSX,
  LC_BUILD_VERSION,
  PLATFORM_MACOS,
  PLATFORM_IOS,
  FAT_MAGIC,
  FAT_MAGIC_64,
  CPU_ARCH_ABI64,
  CPU_TYPE_I386,
  CPU_TYPE_X86_64,
  CPU_TYPE_ARM64,
  CPU_SUBTYPE_ARM64_ALL,
  CPU_SUBTYPE_ARM64_V8,
  CPU_SUBTYPE_ARM64E,
  CPU_SUBTYPE_X86_64_ALL,
  CPU_SUBTYPE_I386_ALL,
  thin,
  fat,
  buildVersion,
  versionMin,
};
