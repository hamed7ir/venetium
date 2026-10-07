// dbgrun.c - Venetium device diagnostics: run venetium.exe under a minimal debugger and save a minidump on a fatal exception.
//
// DEVICE-4 (2026-10-04): on the Surface 2 (Windows 10 build 15035 ARM32) the browser process ends with 0xC0000409 (a fast fail)
// about 1.4 s after start. Crashpad cannot see a fast fail on that build, and Windows Error Reporting wrote no dump. A debugger is
// told about the exception first. This tool starts venetium.exe as a debuggee (only the browser process; its child processes run
// normally, DEBUG_ONLY_THIS_PROCESS), tracks loaded modules, and on a fatal exception writes, into the run folder:
//   crash-<n>.txt   the exception code and name, the fast-fail subcode (for 0xC0000409), the faulting address as module+offset,
//                   and the loaded-module list (base, size, path) for symbolizing with the build's PDBs
//   crash-<n>.dmp   a minidump (dbghelp!MiniDumpWriteDump) with threads, stacks, referenced memory and the module list
// Fatal = a second-chance exception, or a first-chance fast fail / fail-fast / heap-corruption / a breakpoint after the loader's
// initial one. First-chance exceptions are otherwise handed back to the browser (DBG_EXCEPTION_NOT_HANDLED). Everything is also
// written to dbgrun.txt. The tool changes nothing on the machine; it writes only inside the run folder.
//
// Usage: dbgrun.exe <run-folder> <path\to\venetium.exe> [venetium args ...] [--dbgrun-stderr]
//   --dbgrun-stderr (removed before the browser sees its args): send the browser's stdout/stderr to <run-folder>\stderr.txt,
//   where a library prints its message before abort().
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define DBGRUN_VERSION "dbgrun 1 (Venetium device diagnostics, 2026-10-04)"
#define MAX_MODS 2048

typedef struct {
  DWORD_PTR base;
  DWORD size;
  wchar_t path[MAX_PATH];
} Module;

static Module g_mods[MAX_MODS];
static int g_nmods;
static HANDLE g_proc;
static DWORD g_pid;
static FILE* g_log;
static wchar_t g_dir[MAX_PATH];
static int g_reports;
static int g_initial_bp_seen;

static void logf(const char* fmt, ...) {
  va_list ap;
  if (g_log) {
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
  }
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static const wchar_t* base_name(const wchar_t* p) {
  const wchar_t* s = wcsrchr(p, L'\\');
  return s ? s + 1 : p;
}

static void add_module(LPVOID base, HANDLE file) {
  if (g_nmods >= MAX_MODS || !base) return;
  Module* m = &g_mods[g_nmods];
  m->base = (DWORD_PTR)base;
  m->size = 0;
  m->path[0] = 0;
  if (file) {
    DWORD n = GetFinalPathNameByHandleW(file, m->path, MAX_PATH, FILE_NAME_NORMALIZED);
    if (n == 0 || n >= MAX_PATH) m->path[0] = 0;
  }
  if (!m->path[0]) swprintf(m->path, MAX_PATH, L"(0x%p)", base);
  BYTE hdr[1024];
  SIZE_T got = 0;
  if (ReadProcessMemory(g_proc, base, hdr, sizeof hdr, &got) && got >= 0x40) {
    DWORD pe = *(DWORD*)(hdr + 0x3C);
    if ((size_t)pe + 0x54 < got && *(DWORD*)(hdr + pe) == 0x00004550) m->size = *(DWORD*)(hdr + pe + 0x50);
  }
  ++g_nmods;
}

static const Module* module_at(DWORD_PTR a) {
  for (int i = 0; i < g_nmods; ++i)
    if (g_mods[i].size && a >= g_mods[i].base && a < g_mods[i].base + g_mods[i].size) return &g_mods[i];
  return NULL;
}

static const char* code_name(DWORD c) {
  switch (c) {
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN (__fastfail / stack cookie)";
    case 0xC0000602: return "STATUS_FAIL_FAST_EXCEPTION";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xC0000005: return "ACCESS_VIOLATION";
    case 0x80000003: return "BREAKPOINT";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC06D007E: return "delay-load: module not found";
    case 0xC06D007F: return "delay-load: procedure not found";
    case 0xE06D7363: return "C++ exception";
    default: return "";
  }
}

// The subcode of a fast fail (0xC0000409) is in ExceptionInformation[0].
static const char* fastfail_name(ULONG_PTR c) {
  switch (c) {
    case 2: return "STACK_COOKIE_CHECK_FAILURE (/GS)";
    case 3: return "CORRUPT_LIST_ENTRY";
    case 5: return "INVALID_ARG (CRT invalid parameter)";
    case 7: return "FATAL_APP_EXIT (abort())";
    case 10: return "GUARD_ICALL_CHECK_FAILURE (CFG indirect call)";
    case 11: return "GUARD_WRITE_CHECK_FAILURE";
    case 13: return "INVALID_SET_OF_CONTEXT";
    case 30: return "UNHANDLED_C_EXCEPTION_OR_UNWIND";
    case 38: return "LOADER_CONTINUITY_FAILURE";
    default: return "(see the fast-fail code list)";
  }
}

typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
typedef struct {
  DWORD ThreadId;
  EXCEPTION_POINTERS* ExceptionPointers;
  BOOL ClientPointers;
} MDEI;

static void write_dump(const DEBUG_EVENT* ev, int n) {
  HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
  MiniDumpWriteDump_t pMiniDumpWriteDump =
      dbghelp ? (MiniDumpWriteDump_t)(void*)GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
  if (!pMiniDumpWriteDump) {
    logf("  (dbghelp!MiniDumpWriteDump not available - no .dmp)\n");
    return;
  }
  wchar_t path[MAX_PATH + 32];
  swprintf(path, sizeof path / sizeof path[0], L"%ls\\crash-%d.dmp", g_dir, n);
  HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (out == INVALID_HANDLE_VALUE) {
    logf("  (cannot create %ls - no .dmp)\n", path);
    return;
  }
  // MiniDumpWriteDump dereferences ExceptionPointers->ContextRecord; a NULL one gives ERROR_NOACCESS. Capture the faulting
  // thread's context (debugger and debuggee are the same architecture). If that fails, write the dump with no exception stream.
  __declspec(align(16)) CONTEXT ctx;  // x64 CONTEXT must be 16-byte aligned (it has __m128 fields)
  ZeroMemory(&ctx, sizeof ctx);
  ctx.ContextFlags = CONTEXT_FULL;
  HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ev->dwThreadId);
  BOOL have_ctx = th && GetThreadContext(th, &ctx);
  EXCEPTION_POINTERS ep = {(EXCEPTION_RECORD*)&ev->u.Exception.ExceptionRecord, have_ctx ? &ctx : NULL};
  MDEI mdei = {ev->dwThreadId, &ep, FALSE};  // ClientPointers FALSE: the records are in this (debugger) process
  // MiniDumpWithDataSegs|MiniDumpWithHandleData|MiniDumpWithUnloadedModules|MiniDumpWithThreadInfo. (MiniDumpWith-
  // IndirectlyReferencedMemory walks referenced pointers and can hit a freed page -> ERROR_NOACCESS, so it is not used.)
  const int type = 0x0001 | 0x0004 | 0x0020 | 0x1000;
  SetLastError(0);
  BOOL ok = pMiniDumpWriteDump(g_proc, g_pid, out, type, have_ctx ? &mdei : NULL, NULL, NULL);
  DWORD e = GetLastError();
  if (!ok) {  // retry without the exception stream (keeps all threads/stacks; the exception is in crash-N.txt)
    CloseHandle(out);
    out = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out != INVALID_HANDLE_VALUE) {
      SetLastError(0);
      ok = pMiniDumpWriteDump(g_proc, g_pid, out, type, NULL, NULL, NULL);
      DWORD e2 = GetLastError();
      logf("  minidump %ls: %s (with-exception: error 0x%08lX; no-exception: %s 0x%08lX)\n", path,
           ok ? "written (no exception stream)" : "FAILED", e, ok ? "ok" : "error", e2);
      CloseHandle(out);
      if (th) CloseHandle(th);
      return;
    }
  }
  if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
  if (th) CloseHandle(th);
  logf("  minidump %ls: %s%s%s (0x%08lX)\n", path, ok ? "written" : "FAILED", ok ? "" : " error",
       have_ctx ? "" : " [no thread context]", e);
}

static void write_report(const DEBUG_EVENT* ev, int second_chance) {
  const EXCEPTION_RECORD* er = &ev->u.Exception.ExceptionRecord;
  int n = ++g_reports;
  DWORD_PTR addr = (DWORD_PTR)er->ExceptionAddress;
  const Module* m = module_at(addr);
  logf("FATAL exception 0x%08lX %s at 0x%p %s%s (thread %lu, %s chance)\n", er->ExceptionCode,
       code_name(er->ExceptionCode), er->ExceptionAddress, m ? base_name(m->path) : L"(unknown module)",
       "", ev->dwThreadId, second_chance ? "second" : "first");
  wchar_t path[MAX_PATH + 32];
  swprintf(path, sizeof path / sizeof path[0], L"%ls\\crash-%d.txt", g_dir, n);
  FILE* f = _wfopen(path, L"w");
  if (f) {
    fprintf(f, "%s\n\n", DBGRUN_VERSION);
    fprintf(f, "process %lu, thread %lu, %s-chance\n", g_pid, ev->dwThreadId, second_chance ? "second" : "first");
    fprintf(f, "exception code 0x%08lX  %s\n", er->ExceptionCode, code_name(er->ExceptionCode));
    if (er->ExceptionCode == 0xC0000409 && er->NumberParameters >= 1)
      fprintf(f, "fast-fail subcode %llu  %s\n", (unsigned long long)er->ExceptionInformation[0],
              fastfail_name(er->ExceptionInformation[0]));
    if (er->ExceptionCode == 0xC0000005 && er->NumberParameters >= 2)
      fprintf(f, "access violation: %s at 0x%p\n",
              er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
              (void*)er->ExceptionInformation[1]);
    if (m)
      fprintf(f, "address 0x%p = %ls + 0x%lX\n", er->ExceptionAddress, base_name(m->path),
              (unsigned long)(addr - m->base));
    else
      fprintf(f, "address 0x%p (not in a known module)\n", er->ExceptionAddress);
    fprintf(f, "\nloaded modules (%d):\n", g_nmods);
    for (int i = 0; i < g_nmods; ++i)
      fprintf(f, "  0x%p size 0x%-8lX %ls\n", (void*)g_mods[i].base, (unsigned long)g_mods[i].size, g_mods[i].path);
    fclose(f);
    logf("  report %ls written\n", path);
  } else {
    logf("  (cannot write %ls)\n", path);
  }
  write_dump(ev, n);
}

// Build the child command line: "venetium.exe" + the remaining args (argv[2..], minus --dbgrun-stderr). Returns a heap string.
static wchar_t* build_cmdline(int argc, wchar_t** argv, const wchar_t* exe) {
  size_t cap = wcslen(exe) + 8;
  for (int i = 3; i < argc; ++i) cap += wcslen(argv[i]) + 4;
  wchar_t* cmd = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t));
  swprintf(cmd, cap, L"\"%ls\"", exe);
  for (int i = 3; i < argc; ++i) {
    if (!wcscmp(argv[i], L"--dbgrun-stderr")) continue;
    wcscat(cmd, L" ");
    wcscat(cmd, argv[i]);
  }
  return cmd;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    printf("%s\nusage: dbgrun.exe <run-folder> <venetium.exe> [args ...] [--dbgrun-stderr]\n", DBGRUN_VERSION);
    return 2;
  }
  wcsncpy(g_dir, argv[1], MAX_PATH - 1);
  CreateDirectoryW(g_dir, NULL);
  wchar_t logpath[MAX_PATH + 32];
  swprintf(logpath, sizeof logpath / sizeof logpath[0], L"%ls\\dbgrun.txt", g_dir);
  g_log = _wfopen(logpath, L"w");
  logf("%s\n", DBGRUN_VERSION);

  int want_stderr = 0;
  for (int i = 3; i < argc; ++i)
    if (!wcscmp(argv[i], L"--dbgrun-stderr")) want_stderr = 1;

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si);
  ZeroMemory(&pi, sizeof pi);
  si.cb = sizeof si;
  HANDLE err_file = INVALID_HANDLE_VALUE;
  if (want_stderr) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    wchar_t ep[MAX_PATH + 32];
    swprintf(ep, sizeof ep / sizeof ep[0], L"%ls\\stderr.txt", g_dir);
    err_file = CreateFileW(ep, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (err_file != INVALID_HANDLE_VALUE) {
      si.dwFlags |= STARTF_USESTDHANDLES;
      si.hStdOutput = err_file;
      si.hStdError = err_file;
      si.hStdInput = NULL;
    }
  }
  wchar_t* cmd = build_cmdline(argc, argv, argv[2]);
  logf("launching %ls\n", cmd);
  if (!CreateProcessW(argv[2], cmd, NULL, NULL, want_stderr ? TRUE : FALSE,
                      DEBUG_ONLY_THIS_PROCESS | (want_stderr ? 0 : 0), NULL, NULL, &si, &pi)) {
    logf("CreateProcess failed: error %lu\n", GetLastError());
    return 1;
  }
  g_proc = pi.hProcess;
  g_pid = pi.dwProcessId;
  if (err_file != INVALID_HANDLE_VALUE) CloseHandle(err_file);

  DWORD exit_code = 0;
  DEBUG_EVENT ev;
  int running = 1;
  while (running && WaitForDebugEvent(&ev, INFINITE)) {
    DWORD cont = DBG_CONTINUE;
    switch (ev.dwDebugEventCode) {
      case CREATE_PROCESS_DEBUG_EVENT:
        add_module(ev.u.CreateProcessInfo.lpBaseOfImage, ev.u.CreateProcessInfo.hFile);
        if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
        break;
      case LOAD_DLL_DEBUG_EVENT:
        add_module(ev.u.LoadDll.lpBaseOfDll, ev.u.LoadDll.hFile);
        if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        break;
      case EXCEPTION_DEBUG_EVENT: {
        const EXCEPTION_RECORD* er = &ev.u.Exception.ExceptionRecord;
        DWORD c = er->ExceptionCode;
        int first = ev.u.Exception.dwFirstChance;
        int fatal_first = (c == 0xC0000409 || c == 0xC0000602 || c == 0xC0000374 ||
                           (c == 0x80000003 && g_initial_bp_seen));
        if (c == 0x80000003 && !g_initial_bp_seen) {
          g_initial_bp_seen = 1;  // the loader's initial breakpoint: expected, hand it back
          cont = DBG_CONTINUE;
          break;
        }
        if (!first || fatal_first) {
          if (g_reports < 8) write_report(&ev, !first);
          if (!first) {
            // let it die; the browser's exit code follows
            cont = DBG_EXCEPTION_NOT_HANDLED;
          } else {
            // a first-chance fatal: record, then hand back so the normal failure path runs
            cont = DBG_EXCEPTION_NOT_HANDLED;
          }
        } else {
          cont = DBG_EXCEPTION_NOT_HANDLED;  // ordinary first-chance exception: the browser handles it
        }
        break;
      }
      case EXIT_PROCESS_DEBUG_EVENT:
        exit_code = ev.u.ExitProcess.dwExitCode;
        logf("process exited, code 0x%08lX (%ld)\n", exit_code, (long)exit_code);
        running = 0;
        break;
      case OUTPUT_DEBUG_STRING_EVENT:
        break;
      default:
        break;
    }
    ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
  }
  logf("done; %d crash report(s) in %ls\n", g_reports, g_dir);
  if (g_log) fclose(g_log);
  return (int)exit_code;
}
