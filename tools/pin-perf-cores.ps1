# Run one executable pinned to the performance cores of a hybrid CPU.
#
# WHY: the timing-sensitive tests assert wall-clock against audio time, and on a
# hybrid part (performance + efficiency cores) the dominant noise source is the
# process landing on an efficiency core for a whole trial. Measured in this repo
# on identical code: EcosystemEngine SC-011 (a) read 28 528 ns/block on a
# performance core and 50 281 ns/block unpinned -- a 1.76x swing against a
# 53 333 ns/block ceiling, with no code change.
#
# HOW: the performance cores are found by EFFICIENCY CLASS via
# GetLogicalProcessorInformationEx(RelationProcessorCore), not by core index.
# Windows reports a higher EfficiencyClass for the more performant core type, so
# the mask is the union of every core in the highest class. On a part where all
# cores share one class (nothing to pin) the process runs unpinned. Pinning by
# index was rejected earlier because it picked a core without knowing its type.
#
# Usage: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <path> [-ExeArgs a b ...]
# Exit code is the child's. Called by tools/run-cpu-tests.js on Windows.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string[]]$ExeArgs = @()
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class PerfCores
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetLogicalProcessorInformationEx(int relationship, IntPtr buffer, ref uint length);

    // Mask of the logical processors on cores of the highest efficiency class
    // (processor group 0). Returns 0 when every core shares one class or the
    // query fails, meaning: do not pin.
    public static ulong Mask()
    {
        uint len = 0;
        GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref len);
        if (len == 0) return 0;
        IntPtr buf = Marshal.AllocHGlobal((int)len);
        try
        {
            if (!GetLogicalProcessorInformationEx(0, buf, ref len)) return 0;
            // SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX: Relationship @0, Size @4,
            // PROCESSOR_RELATIONSHIP @8: Flags @8, EfficiencyClass @9,
            // Reserved[20] @10, GroupCount @30, GROUP_AFFINITY[] @32:
            // Mask @32 (8 bytes), Group @40.
            var cores = new List<KeyValuePair<byte, ulong>>();
            byte best = 0;
            int off = 0;
            while (off < len)
            {
                int rel = Marshal.ReadInt32(buf, off);
                int size = Marshal.ReadInt32(buf, off + 4);
                if (size <= 0) return 0;
                if (rel == 0)
                {
                    byte cls = Marshal.ReadByte(buf, off + 9);
                    ulong mask = (ulong)Marshal.ReadInt64(buf, off + 32);
                    ushort group = (ushort)Marshal.ReadInt16(buf, off + 40);
                    if (group == 0)
                    {
                        cores.Add(new KeyValuePair<byte, ulong>(cls, mask));
                        if (cls > best) best = cls;
                    }
                }
                off += size;
            }
            ulong result = 0;
            bool mixed = false;
            foreach (var c in cores)
            {
                if (c.Key == best) result |= c.Value; else mixed = true;
            }
            return mixed ? result : 0;
        }
        finally
        {
            Marshal.FreeHGlobal(buf);
        }
    }
}
'@

$mask = [PerfCores]::Mask()
if ($mask -ne 0) {
    Write-Output ("pin-perf-cores: performance-core mask 0x{0:X}" -f $mask)
} else {
    Write-Output "pin-perf-cores: single core class, running unpinned"
}

if ($ExeArgs.Count -gt 0) {
    $p = Start-Process -FilePath $Exe -ArgumentList $ExeArgs -PassThru -NoNewWindow
} else {
    $p = Start-Process -FilePath $Exe -PassThru -NoNewWindow
}
if ($mask -ne 0) {
    # Applied a few microseconds after the child starts; every perf case has a
    # warm-up phase that absorbs that window.
    $p.ProcessorAffinity = [IntPtr][Int64]$mask
}
$p.WaitForExit()
exit $p.ExitCode
