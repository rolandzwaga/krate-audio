// test-crash-dumps.cs: runs one test executable as a debuggee and writes a minidump when it crashes.
//
// Built and used by tools/test-crash-dumps.js (Windows only; compiled once with the .NET Framework csc.exe that
// ships with Windows, cached under the OS temp dir). Usage:
//   test-crash-dumps.exe <dump dir> <exe> [args...]
// The child runs with this process's console, standard handles and environment. Exceptions are passed on to the
// child untouched (first chance: DBG_EXCEPTION_NOT_HANDLED), so its own handlers (Catch2's fatal-condition report,
// SEH, C++ catch) run exactly as without a debugger. Only an exception nothing in the child handled (second chance:
// access violation, stack overflow, heap corruption, __fastfail / abort) is dumped: <dump dir>/<exe>.<pid>.dmp, with
// the faulting thread's exception record and context, then the child is terminated with the exception code. The
// wrapper prints "KRATE CRASH DUMP <path> code=0x........" to stderr and exits with the child's exit code.
//
// No admin rights, no registry, no system-wide setting: WER LocalDumps reads its per-application keys from HKLM
// only (an HKCU key was measured to be ignored, 2026-11-11), so a debugger of our own is the user-level way.
// The caller sets _NO_DEBUG_HEAP=1: a process created under a debugger otherwise gets the slow debug heap.
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

static class TestCrashDumps
{
    const uint DEBUG_ONLY_THIS_PROCESS = 0x00000002;
    const uint EXCEPTION_DEBUG_EVENT = 1, CREATE_PROCESS_DEBUG_EVENT = 3, EXIT_PROCESS_DEBUG_EVENT = 5, LOAD_DLL_DEBUG_EVENT = 6;
    const uint DBG_CONTINUE = 0x00010002, DBG_EXCEPTION_NOT_HANDLED = 0x80010001;
    const uint STATUS_BREAKPOINT = 0x80000003, STATUS_WX86_BREAKPOINT = 0x4000001F;
    const uint INFINITE = 0xFFFFFFFF;
    const int STARTF_USESTDHANDLES = 0x100;
    const uint THREAD_GET_CONTEXT = 0x0008, THREAD_QUERY_INFORMATION = 0x0040;
    const uint CONTEXT_AMD64_ALL = 0x0010001F;
    // MiniDumpNormal | WithIndirectlyReferencedMemory | WithUnloadedModules | WithThreadInfo: stacks, modules and
    // the memory the stacks point at, a few MB (a full dump of a test process runs to hundreds of MB).
    const uint DUMP_TYPE = 0x00000040 | 0x00000020 | 0x00001000;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                      IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool WaitForDebugEvent(IntPtr ev, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool ContinueDebugEvent(uint pid, uint tid, uint status);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr GetStdHandle(int which);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetThreadContext(IntPtr thread, IntPtr ctx);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool MiniDumpWriteDump(IntPtr process, uint pid, IntPtr file, uint type, IntPtr exceptionParam,
                                         IntPtr userStream, IntPtr callback);

    // One argument quoted the way the MSVC runtime's command-line parser reads it back.
    static string Quote(string a)
    {
        if (a.Length > 0 && a.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return a;
        var sb = new StringBuilder("\"");
        for (int i = 0; i < a.Length; ++i)
        {
            int slashes = 0;
            while (i < a.Length && a[i] == '\\') { ++slashes; ++i; }
            if (i == a.Length) { sb.Append('\\', slashes * 2); break; }
            if (a[i] == '"') sb.Append('\\', slashes * 2 + 1).Append('"');
            else sb.Append('\\', slashes).Append(a[i]);
        }
        return sb.Append('"').ToString();
    }

    static int Main(string[] args)
    {
        if (args.Length < 2) { Console.Error.WriteLine("usage: test-crash-dumps <dump dir> <exe> [args...]"); return 2; }
        string dumpDir = args[0], exe = Path.GetFullPath(args[1]);
        var cmd = new StringBuilder(Quote(exe));
        for (int i = 2; i < args.Length; ++i) cmd.Append(' ').Append(Quote(args[i]));

        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(-10);
        si.hStdOutput = GetStdHandle(-11);
        si.hStdError = GetStdHandle(-12);
        PROCESS_INFORMATION pi;
        if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, true, DEBUG_ONLY_THIS_PROCESS, IntPtr.Zero, null, ref si, out pi))
        {
            Console.Error.WriteLine("test-crash-dumps: cannot start " + exe + ": " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
            return 2;
        }
        CloseHandle(pi.hThread);

        IntPtr ev = Marshal.AllocHGlobal(256);  // DEBUG_EVENT is 176 bytes on x64
        IntPtr ctxRaw = Marshal.AllocHGlobal(1232 + 16);  // CONTEXT (x64) is 1232 bytes, 16-byte aligned
        IntPtr ctx = new IntPtr((ctxRaw.ToInt64() + 15) & ~15L);
        IntPtr ptrs = Marshal.AllocHGlobal(16);  // EXCEPTION_POINTERS
        IntPtr mei = Marshal.AllocHGlobal(16);   // MINIDUMP_EXCEPTION_INFORMATION (pack 4)
        bool loaderBreakSeen = false;
        uint exitCode = 0;
        for (;;)
        {
            if (!WaitForDebugEvent(ev, INFINITE)) { exitCode = 2; break; }
            uint code = (uint)Marshal.ReadInt32(ev, 0), pid = (uint)Marshal.ReadInt32(ev, 4), tid = (uint)Marshal.ReadInt32(ev, 8);
            uint status = DBG_CONTINUE;
            if (code == CREATE_PROCESS_DEBUG_EVENT || code == LOAD_DLL_DEBUG_EVENT)
            {
                IntPtr file = Marshal.ReadIntPtr(ev, 16);
                if (file != IntPtr.Zero) CloseHandle(file);
            }
            else if (code == EXIT_PROCESS_DEBUG_EVENT)
            {
                exitCode = (uint)Marshal.ReadInt32(ev, 16);
                ContinueDebugEvent(pid, tid, DBG_CONTINUE);
                break;
            }
            else if (code == EXCEPTION_DEBUG_EVENT)
            {
                IntPtr record = new IntPtr(ev.ToInt64() + 16);  // EXCEPTION_RECORD (152 bytes)
                uint exCode = (uint)Marshal.ReadInt32(record, 0);
                bool firstChance = Marshal.ReadInt32(ev, 168) != 0;
                if (!loaderBreakSeen && (exCode == STATUS_BREAKPOINT || exCode == STATUS_WX86_BREAKPOINT))
                {
                    loaderBreakSeen = true;  // the loader's initial breakpoint
                }
                else if (firstChance)
                {
                    status = DBG_EXCEPTION_NOT_HANDLED;  // the child's own handlers decide
                }
                else
                {
                    string dump = WriteDump(pi.hProcess, pid, tid, exe, dumpDir, record, ctx, ptrs, mei);
                    Console.Error.WriteLine("KRATE CRASH DUMP " + dump + " code=0x" + exCode.ToString("X8"));
                    Console.Error.Flush();
                    TerminateProcess(pi.hProcess, exCode);
                    status = DBG_EXCEPTION_NOT_HANDLED;
                }
            }
            ContinueDebugEvent(pid, tid, status);
        }
        CloseHandle(pi.hProcess);
        return unchecked((int)exitCode);
    }

    static string WriteDump(IntPtr process, uint pid, uint tid, string exe, string dumpDir, IntPtr record, IntPtr ctx,
                            IntPtr ptrs, IntPtr mei)
    {
        try
        {
            Directory.CreateDirectory(dumpDir);
            string path = Path.Combine(Path.GetFullPath(dumpDir), Path.GetFileName(exe) + "." + pid + ".dmp");
            IntPtr thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, false, tid);
            bool haveContext = false;
            if (thread != IntPtr.Zero)
            {
                Marshal.WriteInt32(ctx, 0x30, unchecked((int)CONTEXT_AMD64_ALL));  // CONTEXT.ContextFlags
                haveContext = GetThreadContext(thread, ctx);
                CloseHandle(thread);
            }
            IntPtr exceptionParam = IntPtr.Zero;
            if (haveContext)
            {
                Marshal.WriteIntPtr(ptrs, 0, record);
                Marshal.WriteIntPtr(ptrs, 8, ctx);
                Marshal.WriteInt32(mei, 0, (int)tid);
                Marshal.WriteInt64(mei, 4, ptrs.ToInt64());
                Marshal.WriteInt32(mei, 12, 0);  // ClientPointers FALSE: the pointers are in this process
                exceptionParam = mei;
            }
            using (var fs = new FileStream(path, FileMode.Create, FileAccess.ReadWrite))
            {
                if (!MiniDumpWriteDump(process, pid, fs.SafeFileHandle.DangerousGetHandle(), DUMP_TYPE, exceptionParam,
                                       IntPtr.Zero, IntPtr.Zero))
                    return "(MiniDumpWriteDump failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message + ")";
            }
            return path;
        }
        catch (Exception e)
        {
            return "(dump not written: " + e.Message + ")";
        }
    }
}
