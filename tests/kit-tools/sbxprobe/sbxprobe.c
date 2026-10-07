// sbxprobe.c - Venetium device probe: which part of Chromium's sandboxed process creation does this Windows reject?
//
// Why (2026-10-04): on the Surface 2 (Windows 10 build 15035, ARM32) every sandboxed child launch of Venetium fails in
// CreateProcessAsUserW with ERROR_INVALID_PARAMETER (87) - Chromium's metrics: Process.Sandbox.Launch.Error = 87,
// SBOX_ERROR_CREATE_PROCESS. This probe repeats that call piece by piece, with the probe itself as a do-nothing child
// ("--child" exits at once with code 42), and logs for every variant whether Windows accepts it. A control replays the
// call with the real venetium.exe (next to the probe, or one or two folders up) and Chromium-like handles; that child is
// created suspended and ended without ever running.
//
// It changes nothing on the machine: it only creates (and ends) its own child processes, plus job objects, restricted
// tokens, a window station, two desktops, a pipe, sections and events that exist only while the probe runs. No registry
// writes, no files except its log.
//
// Usage: sbxprobe.exe [log-directory]          (default: the folder that holds sbxprobe.exe; then %TEMP%; then the
//                                               current directory)
//        sbxprobe.exe --child                  (internal)
//        sbxprobe.exe --apply <n> [--revert]   (internal, section 6: apply one mitigation to itself)
//        sbxprobe.exe --report                 (internal, section 5: exit 0x3xxxx with its own policy bits)
//
// Version 2 (review wf_50f0e839-955): no hard-error / WER dialogs (SetErrorMode, inherited by the children); call-shape
// baselines (section 0); sections 2 and 3 also through CreateProcessAsUserW; every row has an expectation and only
// UNEXPECTED rows are listed at the end; the replay names what makes Windows accept the call (DIAGNOSIS); real-image control
// with Chromium-like handles, Chromium's alternate window station and filtered environment; advapi32's
// CreateProcessAsUserW (what chrome.dll imports); log fallback; UTF-8 output.
//
// Version 3 (BATCH-ARM-FIX-2, after the DEVICE-3 trip showed 15035 ARM32 refuses MITIGATION_POLICY in the 8- and 16-byte forms):
// section 5 measures the older 4-byte (DWORD) form, single values and Chromium's full call; section 6 measures the mitigations a
// sandboxed child applies to itself after startup (Chromium's delayed route; Chromium PCHECKs those calls). Summary block
// "PROBE v3" at the end.
// 3.1 (review wf_8ca010d1-e4a): section 6 also under a USER_LOCKDOWN-like primary token with the initial token on the main
// thread and RevertToSelf in the child; honest read-backs (41 no read-back exists, 0x20000 read-back failed); section 5 effect
// checks (--report child vs a plain baseline), DEP rows, CREATE_BREAKAWAY_FROM_JOB rows, leave-one-out for a refused 4-byte
// full call; QuickEdit off during the run.
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A00000C
#include <windows.h>
#include <io.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define PROBE_VERSION "sbxprobe 3.1 (Venetium ARM-FIX-2, 2026-10-04)"
#define CHILD_EXIT 42
#define CHILD_EXIT_NO_READBACK 41               // --apply: the call returned TRUE; no read-back exists for it
#define CHILD_EXIT_NOT_VISIBLE 43               // --apply: the call succeeded, the read-back does not show it
#define CHILD_EXIT_CALL_FAILED 0x00010000u      // --apply: | GetLastError() & 0xFFFF
#define CHILD_EXIT_READBACK_FAILED 0x00020000u  // --apply: the read-back call itself failed | GetLastError() & 0xFFFF
#define CHILD_EXIT_REPORT 0x00030000u           // --report: | the child's own policy bits (section 5)

static FILE* g_log;
static wchar_t g_exe[MAX_PATH];
static wchar_t g_image[MAX_PATH];  // venetium.exe next to the probe or one or two folders up, if present
static int g_tests, g_created, g_ran_ok, g_child_failed, g_rej87, g_other, g_unexpected;
static int g_last_result;  // last run_test: 0 created (accepted), 87 rejected with 87, 1 other failure
static const char* g_last_verdict = "";  // last run_test's verdict word (OK, CHILD-CALL-FAILED, ...)
static DWORD g_last_exit;                 // last run_test's child exit code (0 if no child ran)
static char g_last_detail[256];           // last run_test's detail text

static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_log) {
    vfprintf(g_log, fmt, ap);
    fflush(g_log);
  }
  va_end(ap);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

// Growable-enough text buffers for the end-of-run lists (heap, 256 KB each; the log always has everything).
typedef struct {
  char* p;
  size_t len, cap;
} Buf;
static Buf g_unexp, g_diag, g_v3;  // g_v3: the version-3 measurements (sections 5 and 6), summarised at the end

static void buf_init(Buf* b, size_t cap) {
  b->p = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
  b->cap = b->p ? cap : 0;
  b->len = 0;
}

static void buf_add(Buf* b, const char* fmt, ...) {
  if (!b->p || b->len + 1 >= b->cap) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t room = b->cap - b->len - 1;
  b->len += (size_t)n < room ? (size_t)n : room;
}

static const char* errname(DWORD e) {
  switch (e) {
    case 0: return "OK";
    case 2: return "FILE_NOT_FOUND";
    case 5: return "ACCESS_DENIED";
    case 6: return "INVALID_HANDLE";
    case 8: return "NOT_ENOUGH_MEMORY";
    case 50: return "NOT_SUPPORTED";
    case 87: return "INVALID_PARAMETER";
    case 122: return "INSUFFICIENT_BUFFER";
    case 193: return "BAD_EXE_FORMAT";
    case 1314: return "PRIVILEGE_NOT_HELD";
    case 1349: return "BAD_TOKEN_TYPE";
    case 577: return "INVALID_IMAGE_HASH";
    case 14001: return "SXS_CANT_GEN_ACTCTX (side-by-side manifest: is the <version>.manifest next to venetium.exe?)";
    default: return "";
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// One test = one CreateProcess(AsUser)W of ourselves (or of venetium.exe, never run) with the given call shape.
typedef struct {
  DWORD_PTR attribute;
  void* value;
  SIZE_T size;
  int tolerate_not_supported;  // like Chromium for COMPONENT_FILTER: Update failing with ERROR_NOT_SUPPORTED = left out
} Attr;

static Attr mk(DWORD_PTR attribute, void* value, SIZE_T size, int tolerate) {
  Attr a;
  a.attribute = attribute;
  a.value = value;
  a.size = size;
  a.tolerate_not_supported = tolerate;
  return a;
}

enum { EXPECT_OK, EXPECT_87, EXPECT_ANY };

typedef struct {
  const Attr* attrs;
  int nattrs;
  HANDLE token;              // NULL: CreateProcessW; else CreateProcessAsUserW (advapi32's, as chrome.dll imports it)
  const wchar_t* desktop;    // STARTUPINFO.lpDesktop (NULL = inherit)
  BOOL inherit;              // bInheritHandles
  DWORD flags;               // dwCreationFlags; EXTENDED_STARTUPINFO_PRESENT is added when there are attributes
  DWORD startf;              // STARTUPINFO.dwFlags; the standard handles stay NULL
  void* env;                 // lpEnvironment (NULL = inherit)
  const wchar_t* image_args; // non-NULL: venetium.exe with these arguments, created suspended and ended unrun
  const wchar_t* child_args; // probe child's arguments (NULL = "--child"); "--apply N" = section 6, "--report" = section 5
  HANDLE thread_token;       // impersonation token set on the suspended main thread before it runs (Chromium's initial token)
  int expect;
} Call;

static Call call0(void) {
  Call c;
  ZeroMemory(&c, sizeof c);
  c.flags = CREATE_SUSPENDED | DETACHED_PROCESS;  // sections 1-3; section 0 varies it
  return c;
}

typedef BOOL(WINAPI* CreateProcessAsUserW_t)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
                                             DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
static CreateProcessAsUserW_t pCreateProcessAsUserW;
typedef BOOL(WINAPI* SetThreadToken_t)(PHANDLE, HANDLE);
static SetThreadToken_t pSetThreadToken;  // advapi32, resolved in load_advapi (section 6 lockdown pass)
static char g_note[160];

// returns 1 if Windows created the process (accepted the call), 0 if not; g_last_result says how it failed
static int run_test(const char* name, const Call* c) {
  ++g_tests;
  g_note[0] = 0;
  SIZE_T list_size = 0;
  LPPROC_THREAD_ATTRIBUTE_LIST list = NULL;
  DWORD upd_err = 0, create_err = 0, exit_code = 0;
  int failed_attr = -1, created = 0, ran_ok = 0;
  if (c->nattrs > 0) {
    InitializeProcThreadAttributeList(NULL, (DWORD)c->nattrs, 0, &list_size);
    list = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, list_size ? list_size : 64);
    if (!list || !InitializeProcThreadAttributeList(list, (DWORD)c->nattrs, 0, &list_size)) {
      upd_err = GetLastError();
      failed_attr = -2;
    } else {
      for (int i = 0; i < c->nattrs; ++i) {
        const Attr* a = &c->attrs[i];
        if (!UpdateProcThreadAttribute(list, 0, a->attribute, a->value, a->size, NULL, NULL)) {
          DWORD e = GetLastError();
          if (e == ERROR_NOT_SUPPORTED && a->tolerate_not_supported) {
            snprintf(g_note, sizeof g_note, " [attribute 0x%lX: Update said NOT_SUPPORTED - left out, as Chromium does]",
                     (unsigned long)a->attribute);
            continue;
          }
          upd_err = e;
          failed_attr = i;
          break;
        }
      }
    }
  }
  const char* verdict;
  char detail[256] = "";
  if (failed_attr != -1) {
    verdict = upd_err == 87 ? "REJECTED-AT-UPDATE(87)" : "FAILED-AT-UPDATE";
    snprintf(detail, sizeof detail, "%s #%d error %lu %s",
             failed_attr == -2 ? "InitializeProcThreadAttributeList" : "UpdateProcThreadAttribute", failed_attr, upd_err,
             errname(upd_err));
  } else {
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    ZeroMemory(&pi, sizeof pi);
    si.StartupInfo.cb = c->nattrs > 0 ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
    si.lpAttributeList = list;
    si.StartupInfo.lpDesktop = (LPWSTR)c->desktop;
    si.StartupInfo.dwFlags = c->startf;  // hStdInput/hStdOutput/hStdError stay NULL
    const wchar_t* app = c->image_args ? g_image : g_exe;
    wchar_t cmd[2 * MAX_PATH + 160];
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" %ls", app,
             c->image_args ? c->image_args : c->child_args ? c->child_args : L"--child");
    DWORD flags = c->flags | (c->nattrs > 0 ? EXTENDED_STARTUPINFO_PRESENT : 0);
    if (c->image_args) flags |= CREATE_SUSPENDED;  // the real browser must never run
    BOOL ok;
    SetLastError(0);
    if (c->token)
      ok = pCreateProcessAsUserW ? pCreateProcessAsUserW(c->token, app, cmd, NULL, NULL, c->inherit, flags, c->env, NULL,
                                                         &si.StartupInfo, &pi)
                                 : (SetLastError(ERROR_PROC_NOT_FOUND), FALSE);
    else
      ok = CreateProcessW(app, cmd, NULL, NULL, c->inherit, flags, c->env, NULL, &si.StartupInfo, &pi);
    if (!ok) {
      create_err = GetLastError();
      verdict = create_err == 87 ? "REJECTED(87)" : "CREATE-FAILED";
      snprintf(detail, sizeof detail, "CreateProcess%s error %lu %s", c->token ? "AsUserW" : "W", create_err,
               errname(create_err));
    } else {
      created = 1;
      if (c->image_args) {
        TerminateProcess(pi.hProcess, CHILD_EXIT);
        WaitForSingleObject(pi.hProcess, 5000);
        verdict = "CREATED";
        ran_ok = 1;
        snprintf(detail, sizeof detail, "venetium.exe created suspended, then ended without running");
      } else {
        if (c->thread_token) {  // like Chromium's TargetProcess: the main thread starts under the initial (impersonation) token
          HANDLE th = pi.hThread;
          if (!pSetThreadToken || !pSetThreadToken(&th, c->thread_token))
            snprintf(g_note, sizeof g_note, " [SetThreadToken failed: error %lu]", GetLastError());
        }
        ResumeThread(pi.hThread);  // harmless when the call shape had no CREATE_SUSPENDED
        DWORD w = WaitForSingleObject(pi.hProcess, 10000);
        if (w != WAIT_OBJECT_0) {
          TerminateProcess(pi.hProcess, 1);
          WaitForSingleObject(pi.hProcess, 5000);
          verdict = "CREATED-CHILD-HUNG";
          snprintf(detail, sizeof detail, "child still running after 10 s (terminated)");
        } else {
          GetExitCodeProcess(pi.hProcess, &exit_code);
          ran_ok = exit_code == CHILD_EXIT;
          verdict = ran_ok ? "OK" : "CREATED-CHILD-FAILED";
          if (c->child_args && exit_code == CHILD_EXIT_NOT_VISIBLE) {
            verdict = "APPLIED-NOT-VISIBLE";
            snprintf(detail, sizeof detail, "the child's call succeeded, but reading the policy back does not show it");
          } else if (c->child_args && exit_code == CHILD_EXIT_NO_READBACK) {
            verdict = "OK-NO-READBACK";
            ran_ok = 1;
            snprintf(detail, sizeof detail, "the child's call returned TRUE (no read-back exists for this call)");
          } else if (c->child_args && (exit_code & 0xFFFF0000u) == CHILD_EXIT_CALL_FAILED) {
            verdict = "CHILD-CALL-FAILED";
            snprintf(detail, sizeof detail, "the child's call failed: error %lu %s", exit_code & 0xFFFF,
                     errname(exit_code & 0xFFFF));
          } else if (c->child_args && (exit_code & 0xFFFF0000u) == CHILD_EXIT_READBACK_FAILED) {
            verdict = "READBACK-FAILED";
            snprintf(detail, sizeof detail, "the child's call succeeded; reading it back failed: error %lu %s",
                     exit_code & 0xFFFF, errname(exit_code & 0xFFFF));
          } else if (c->child_args && (exit_code & 0xFFFF0000u) == CHILD_EXIT_REPORT) {
            verdict = "REPORTED";
            ran_ok = 1;
            snprintf(detail, sizeof detail, "the child reported its own policy bits 0x%04lX", exit_code & 0xFFFF);
          } else {
            snprintf(detail, sizeof detail, "child exit code 0x%08lX%s", exit_code,
                     ran_ok ? (c->child_args ? " (applied, read back)" : "")
                            : " (created, but the child did not run normally)");
          }
        }
      }
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }
  if (list) {
    if (failed_attr != -2) DeleteProcThreadAttributeList(list);
    HeapFree(GetProcessHeap(), 0, list);
  }
  int result = created ? 0 : (upd_err == 87 || create_err == 87) ? 87 : 1;
  int unexpected = (c->expect == EXPECT_OK && result != 0) || (c->expect == EXPECT_87 && result != 87);
  const char* mark = unexpected ? "  <-- UNEXPECTED" : (c->expect == EXPECT_87 && result == 87) ? "  (expected)" : "";
  say("T%03d %-22s %s | %s%s%s\n", g_tests, verdict, name, detail, g_note, mark);
  if (result == 0) {
    ++g_created;
    if (ran_ok) ++g_ran_ok; else ++g_child_failed;
  } else if (result == 87) {
    ++g_rej87;
  } else {
    ++g_other;
  }
  if (unexpected) {
    ++g_unexpected;
    buf_add(&g_unexp, "  T%03d %s: %s %s\n", g_tests, name, verdict, detail);
  }
  g_last_result = result;
  g_last_verdict = verdict;
  g_last_exit = exit_code;
  snprintf(g_last_detail, sizeof g_last_detail, "%s", detail);
  return created;
}

static HANDLE g_pass_token;  // sections 2 and 3: NULL (CreateProcessW) or the own-token duplicate (CreateProcessAsUserW)

static const char* pass_name(void) { return g_pass_token ? " [AsUserW]" : ""; }

static void run_attr(const char* name, Attr a, BOOL inherit, int expect) {
  Call c = call0();
  c.attrs = &a;
  c.nattrs = 1;
  c.token = g_pass_token;
  c.inherit = inherit;
  c.expect = expect;
  char full[320];
  snprintf(full, sizeof full, "%s%s", name, pass_name());
  run_test(full, &c);
}

static void run_mitigation(const char* name, DWORD64 v1, DWORD64 v2, SIZE_T size, int expect) {
  DWORD64 val[2] = {v1, v2};
  char full[240];
  snprintf(full, sizeof full, "MITIGATION_POLICY size %u value1 0x%016llX value2 0x%016llX %s", (unsigned)size,
           (unsigned long long)v1, (unsigned long long)v2, name);
  run_attr(full, mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, val, size, 0), FALSE, expect);
}

// ---------------------------------------------------------------------------------------------------------------------
static const char* kV1Fields[16] = {"DEP/ATL/SEHOP bits", "(unused)", "FORCE_RELOCATE_IMAGES", "HEAP_TERMINATE",
                                    "BOTTOM_UP_ASLR", "HIGH_ENTROPY_ASLR", "STRICT_HANDLE_CHECKS",
                                    "WIN32K_SYSTEM_CALL_DISABLE", "EXTENSION_POINT_DISABLE", "PROHIBIT_DYNAMIC_CODE",
                                    "CONTROL_FLOW_GUARD", "BLOCK_NON_MICROSOFT_BINARIES", "FONT_DISABLE",
                                    "IMAGE_LOAD_NO_REMOTE", "IMAGE_LOAD_NO_LOW_LABEL", "IMAGE_LOAD_PREFER_SYSTEM32"};
static const char* kV2Fields[16] = {"(unused)", "LOADER_INTEGRITY_CONTINUITY", "STRICT_CONTROL_FLOW_GUARD",
                                    "MODULE_TAMPERING_PROTECTION", "RESTRICT_INDIRECT_BRANCH_PREDICTION",
                                    "ALLOW_DOWNGRADE_DYNAMIC_CODE_POLICY", "SPECULATIVE_STORE_BYPASS_DISABLE",
                                    "CET_USER_SHADOW_STACKS", "USER_CET_SET_CONTEXT_IP_VALIDATION",
                                    "BLOCK_NON_CET_BINARIES", "XTENDED_CONTROL_FLOW_GUARD", "POINTER_AUTH_USER_IP",
                                    "CET_DYNAMIC_APIS_OUT_OF_PROC_ONLY", "RESTRICT_CORE_SHARING",
                                    "FSCTL_SYSTEM_CALL_DISABLE", "(unused)"};
static const char* kNibbleVal[4] = {"0", "ALWAYS_ON(1)", "ALWAYS_OFF(2)", "VALUE3"};
// every value1 bit any Chromium process type sets on this OS (see replay.inc)
static const DWORD64 kChromiumV1 = 0x0111100110011000ull;

static DWORD64 g_supported[2];
static BOOL g_supported_ok8, g_supported_ok16;

static void environment(void) {
  typedef LONG(WINAPI * RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW v;
  ZeroMemory(&v, sizeof v);
  v.dwOSVersionInfoSize = sizeof v;
  RtlGetVersion_t rgv = (RtlGetVersion_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  if (rgv) rgv(&v);
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  const char* arch = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM     ? "ARM32"
                     : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86"
                     : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64"
                     : si.wProcessorArchitecture == 12                           ? "ARM64"
                                                                                 : "?";
  say("%s\n", PROBE_VERSION);
#if defined(SBX_INJECT)
  say("TEST BUILD: faults injected into the replay (SBX_INJECT=%d) - not for the device\n", SBX_INJECT);
#endif
  SYSTEMTIME t;
  GetLocalTime(&t);
  say("time (local) %04u-%02u-%02u %02u:%02u:%02u\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  say("windows %lu.%lu.%lu  native arch %s, %lu CPUs\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, arch,
      si.dwNumberOfProcessors);
#if defined(_M_ARM)
  say("probe built for ARM32\n");
#elif defined(_M_IX86)
  say("probe built for x86\n");
#elif defined(_M_X64)
  say("probe built for x64\n");
#endif
  say("probe exe: %ls\n", g_exe);
  if (g_image[0])
    say("venetium.exe for the real-image control: %ls\n", g_image);
  else
    say("venetium.exe NOT found next to the probe or one or two folders up - the real-image control rows are skipped\n");
  BOOL in_job = FALSE;
  IsProcessInJob(GetCurrentProcess(), NULL, &in_job);
  say("this probe runs inside a job: %s\n", in_job ? "yes" : "no");
  DWORD64 m[2] = {0, 0};
  g_supported_ok8 = GetProcessMitigationPolicy(GetCurrentProcess(), ProcessMitigationOptionsMask, m, sizeof(DWORD64));
  DWORD e8 = GetLastError();
  say("supported mitigations (ProcessMitigationOptionsMask, 8 bytes): %s value1 0x%016llX\n",
      g_supported_ok8 ? "ok" : "FAILED", (unsigned long long)m[0]);
  if (!g_supported_ok8) say("   error %lu %s\n", e8, errname(e8));
  g_supported[0] = m[0];
  DWORD64 m2[2] = {0, 0};
  g_supported_ok16 = GetProcessMitigationPolicy(GetCurrentProcess(), ProcessMitigationOptionsMask, m2, sizeof m2);
  DWORD e16 = GetLastError();
  say("supported mitigations (16 bytes): %s value1 0x%016llX value2 0x%016llX\n", g_supported_ok16 ? "ok" : "FAILED",
      (unsigned long long)m2[0], (unsigned long long)m2[1]);
  if (!g_supported_ok16) say("   error %lu %s\n", e16, errname(e16));
  if (g_supported_ok16) g_supported[1] = m2[1];
  if (!g_supported_ok8 && g_supported_ok16) {
    g_supported[0] = m2[0];
    say("   (the 8-byte query failed: using the 16-byte value1 as the supported mask)\n");
  }
  if (!g_supported_ok8 && !g_supported_ok16)
    say("   WARNING: both queries failed - Chromium would stop here (NOTREACHED); the replay passes no MITIGATION_POLICY\n");
  for (int k = 0; k < 16; ++k) {
    unsigned n1 = (unsigned)((g_supported[0] >> (4 * k)) & 0xF);
    unsigned n2 = (unsigned)((g_supported[1] >> (4 * k)) & 0xF);
    if (n1 || n2)
      say("   field %2d: value1 nibble 0x%X (%s)   value2 nibble 0x%X (%s)\n", k, n1, kV1Fields[k], n2, kV2Fields[k]);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Chromium replay table: filled from the source (F:\cr\sbxprobe\spec, sandbox/policy/win + sandbox/win/src at
// state-0028) - see replay.inc.
typedef struct {
  const char* type;
  const wchar_t* args;       // venetium.exe arguments for the real-image control row (NULL = no control row)
  DWORD64 requested_v1;      // Chromium's policy_value_1 before the supported-mask AND
  DWORD64 requested_v2;      // policy_value_2 (0 on RS1/RS2 builds)
  int child_process_policy;  // PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY = RESTRICTED
  int job_list;              // PROC_THREAD_ATTRIBUTE_JOB_LIST: 0 none, 1 limited-user job (GPU), 2 lockdown job
  int handle_list;           // PROC_THREAD_ATTRIBUTE_HANDLE_LIST (+ bInheritHandles): probe rows one inheritable event,
                             // venetium.exe rows Chromium-like handles
  int component_filter;      // PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER = COMPONENT_KTM (NOT_SUPPORTED tolerated)
  int token;                 // 0 none (CreateProcessW), 1 own token dup, 2 restricted (no admin, disable max priv),
                             // 3 = 2 + low integrity, 4 = 2 + untrusted integrity + restricting SIDs,
                             // 5 = Chromium USER_LOCKDOWN-like, 6 = Chromium USER_LIMITED-like
  int desk;                  // 0 default desktop, 1 alternate desktop in this window station,
                             // 2 alternate window station + desktop (Chromium)
  int env;                   // 1 = filtered environment block (Chromium SetFilterEnvironment)
  int extra;                 // venetium.exe rows: 1 = + utility bootstrap event, 2 = + two SEC_IMAGE sections (CIG)
} Replay;
#include "replay.inc"

// ---------------------------------------------------------------------------------------------------------------------
typedef BOOL(WINAPI* CreateRestrictedToken_t)(HANDLE, DWORD, DWORD, PSID_AND_ATTRIBUTES, DWORD, PLUID_AND_ATTRIBUTES,
                                              DWORD, PSID_AND_ATTRIBUTES, PHANDLE);
typedef BOOL(WINAPI* OpenProcessToken_t)(HANDLE, DWORD, PHANDLE);
typedef BOOL(WINAPI* DuplicateTokenEx_t)(HANDLE, DWORD, LPSECURITY_ATTRIBUTES, SECURITY_IMPERSONATION_LEVEL, TOKEN_TYPE,
                                         PHANDLE);
typedef BOOL(WINAPI* SetTokenInformation_t)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD);
typedef BOOL(WINAPI* AllocateAndInitializeSid_t)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                 DWORD, DWORD, DWORD, PSID*);
typedef PVOID(WINAPI* FreeSid_t)(PSID);
typedef BOOL(WINAPI* GetTokenInformation_t)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
static CreateRestrictedToken_t pCreateRestrictedToken;
static OpenProcessToken_t pOpenProcessToken;
static DuplicateTokenEx_t pDuplicateTokenEx;
static SetTokenInformation_t pSetTokenInformation;
static AllocateAndInitializeSid_t pAllocateAndInitializeSid;
static FreeSid_t pFreeSid;
static GetTokenInformation_t pGetTokenInformation;

static void module_of(const void* p, wchar_t* out, DWORD n) {
  HMODULE h = NULL;
  out[0] = 0;
  if (p && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)p, &h)) {
    wchar_t full[MAX_PATH];
    GetModuleFileNameW(h, full, MAX_PATH);
    const wchar_t* s = wcsrchr(full, L'\\');
    wcsncpy(out, s ? s + 1 : full, n - 1);
    out[n - 1] = 0;
  }
}

static void load_advapi(void) {
  HMODULE adv = LoadLibraryW(L"advapi32.dll");
  CreateProcessAsUserW_t a = adv ? (CreateProcessAsUserW_t)(void*)GetProcAddress(adv, "CreateProcessAsUserW") : NULL;
  CreateProcessAsUserW_t k =
      (CreateProcessAsUserW_t)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateProcessAsUserW");
  pCreateProcessAsUserW = a ? a : k;  // chrome.dll imports ADVAPI32!CreateProcessAsUserW
  wchar_t ma[64], mk32[64];
  module_of((const void*)a, ma, 64);
  module_of((const void*)k, mk32, 64);
  say("CreateProcessAsUserW: advapi32 export %p (code in %ls), kernel32 export %p (code in %ls); the probe uses %s\n",
      (void*)a, ma[0] ? ma : L"-", (void*)k, mk32[0] ? mk32 : L"-",
      a ? "advapi32's, as chrome.dll does" : k ? "kernel32's (advapi32 has none)" : "NONE - AsUser rows fail");
  if (adv) {
    pCreateRestrictedToken = (CreateRestrictedToken_t)(void*)GetProcAddress(adv, "CreateRestrictedToken");
    pOpenProcessToken = (OpenProcessToken_t)(void*)GetProcAddress(adv, "OpenProcessToken");
    pDuplicateTokenEx = (DuplicateTokenEx_t)(void*)GetProcAddress(adv, "DuplicateTokenEx");
    pSetTokenInformation = (SetTokenInformation_t)(void*)GetProcAddress(adv, "SetTokenInformation");
    pAllocateAndInitializeSid = (AllocateAndInitializeSid_t)(void*)GetProcAddress(adv, "AllocateAndInitializeSid");
    pFreeSid = (FreeSid_t)(void*)GetProcAddress(adv, "FreeSid");
    pGetTokenInformation = (GetTokenInformation_t)(void*)GetProcAddress(adv, "GetTokenInformation");
    pSetThreadToken = (SetThreadToken_t)(void*)GetProcAddress(adv, "SetThreadToken");
  }
  say("CreateRestrictedToken %s\n", pCreateRestrictedToken ? "found" : "MISSING");
}

static void set_integrity(HANDLE tok, DWORD rid);

// Chromium USER_LOCKDOWN-like primary token (sandbox restricted_token_utils.cc): DISABLE_MAX_PRIVILEGE, every group of
// the user (and the user SID) deny-only, the single restricting SID Null (S-1-0-0), low integrity.
static HANDLE make_lockdown_token(HANDLE own) {
  if (!pGetTokenInformation || !pCreateRestrictedToken || !pAllocateAndInitializeSid) return NULL;
  DWORD len = 0;
  pGetTokenInformation(own, TokenGroups, NULL, 0, &len);
  TOKEN_GROUPS* g = (TOKEN_GROUPS*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len ? len : 1);
  BYTE userbuf[256];
  DWORD ulen = 0;
  if (!g || !pGetTokenInformation(own, TokenGroups, g, len, &len) ||
      !pGetTokenInformation(own, TokenUser, userbuf, sizeof userbuf, &ulen)) {
    say("   GetTokenInformation error %lu\n", GetLastError());
    if (g) HeapFree(GetProcessHeap(), 0, g);
    return NULL;
  }
  SID_AND_ATTRIBUTES* deny = (SID_AND_ATTRIBUTES*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                             sizeof(SID_AND_ATTRIBUTES) * (g->GroupCount + 1));
  DWORD nd = 0;
  deny[nd++].Sid = ((TOKEN_USER*)userbuf)->User.Sid;
  for (DWORD i = 0; i < g->GroupCount; ++i) {
    if (g->Groups[i].Attributes & (SE_GROUP_INTEGRITY | SE_GROUP_LOGON_ID)) continue;  // labels / logon SID stay
    deny[nd++].Sid = g->Groups[i].Sid;
  }
  SID_IDENTIFIER_AUTHORITY nullauth = {SECURITY_NULL_SID_AUTHORITY};
  PSID nullsid = NULL;
  pAllocateAndInitializeSid(&nullauth, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &nullsid);
  SID_AND_ATTRIBUTES restrict_sids[1] = {{nullsid, 0}};
  HANDLE tok = NULL;
  if (!pCreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, nd, deny, 0, NULL, nullsid ? 1 : 0, nullsid ? restrict_sids : NULL,
                              &tok)) {
    say("   CreateRestrictedToken(lockdown) error %lu\n", GetLastError());
    tok = NULL;
  }
  if (tok) set_integrity(tok, SECURITY_MANDATORY_LOW_RID);
  if (nullsid) pFreeSid(nullsid);
  HeapFree(GetProcessHeap(), 0, deny);
  HeapFree(GetProcessHeap(), 0, g);
  return tok;
}

// Chromium USER_LIMITED-like primary token (GPU): DISABLE_MAX_PRIVILEGE, every group deny-only except Users / Everyone /
// Interactive, restricting SIDs {Users, Everyone, RESTRICTED, logon SID}, low integrity. (Chromium also adds a random
// per-launch restricting SID; not reproduced.)
static int sid_is(PSID sid, BYTE auth, DWORD rid0, DWORD rid1, int nsub) {
  SID_IDENTIFIER_AUTHORITY* ia = GetSidIdentifierAuthority(sid);
  if (!ia || ia->Value[5] != auth || *GetSidSubAuthorityCount(sid) != nsub) return 0;
  if (*GetSidSubAuthority(sid, 0) != rid0) return 0;
  return nsub < 2 || *GetSidSubAuthority(sid, 1) == rid1;
}

static HANDLE make_limited_token(HANDLE own) {
  if (!pGetTokenInformation || !pCreateRestrictedToken || !pAllocateAndInitializeSid) return NULL;
  DWORD len = 0;
  pGetTokenInformation(own, TokenGroups, NULL, 0, &len);
  TOKEN_GROUPS* g = (TOKEN_GROUPS*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len ? len : 1);
  BYTE userbuf[256];
  DWORD ulen = 0;
  if (!g || !pGetTokenInformation(own, TokenGroups, g, len, &len) ||
      !pGetTokenInformation(own, TokenUser, userbuf, sizeof userbuf, &ulen)) {
    if (g) HeapFree(GetProcessHeap(), 0, g);
    return NULL;
  }
  SID_AND_ATTRIBUTES* deny = (SID_AND_ATTRIBUTES*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                             sizeof(SID_AND_ATTRIBUTES) * (g->GroupCount + 1));
  SID_AND_ATTRIBUTES restrict_sids[4];
  DWORD nd = 0, nr = 0;
  PSID logon = NULL;
  for (DWORD i = 0; i < g->GroupCount; ++i) {
    PSID s = g->Groups[i].Sid;
    if (g->Groups[i].Attributes & SE_GROUP_INTEGRITY) continue;
    if (g->Groups[i].Attributes & SE_GROUP_LOGON_ID) {
      logon = s;
      continue;
    }
    if (sid_is(s, 5, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 2) || sid_is(s, 1, SECURITY_WORLD_RID, 0, 1) ||
        sid_is(s, 5, SECURITY_INTERACTIVE_RID, 0, 1))
      continue;
    deny[nd++].Sid = s;
  }
  SID_IDENTIFIER_AUTHORITY nt = {SECURITY_NT_AUTHORITY}, world = {SECURITY_WORLD_SID_AUTHORITY};
  PSID users = NULL, everyone = NULL, restricted = NULL;
  pAllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users);
  pAllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone);
  pAllocateAndInitializeSid(&nt, 1, SECURITY_RESTRICTED_CODE_RID, 0, 0, 0, 0, 0, 0, 0, &restricted);
  if (users) restrict_sids[nr].Sid = users, restrict_sids[nr++].Attributes = 0;
  if (everyone) restrict_sids[nr].Sid = everyone, restrict_sids[nr++].Attributes = 0;
  if (restricted) restrict_sids[nr].Sid = restricted, restrict_sids[nr++].Attributes = 0;
  if (logon) restrict_sids[nr].Sid = logon, restrict_sids[nr++].Attributes = 0;
  HANDLE tok = NULL;
  if (!pCreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, nd, deny, 0, NULL, nr, restrict_sids, &tok)) {
    say("   CreateRestrictedToken(limited) error %lu\n", GetLastError());
    tok = NULL;
  }
  if (tok) set_integrity(tok, SECURITY_MANDATORY_LOW_RID);
  if (users) pFreeSid(users);
  if (everyone) pFreeSid(everyone);
  if (restricted) pFreeSid(restricted);
  HeapFree(GetProcessHeap(), 0, deny);
  HeapFree(GetProcessHeap(), 0, g);
  return tok;
}

static void set_integrity(HANDLE tok, DWORD rid) {
  SID_IDENTIFIER_AUTHORITY ml = {SECURITY_MANDATORY_LABEL_AUTHORITY};
  PSID il = NULL;
  if (pAllocateAndInitializeSid && pSetTokenInformation &&
      pAllocateAndInitializeSid(&ml, 1, rid, 0, 0, 0, 0, 0, 0, 0, &il)) {
    TOKEN_MANDATORY_LABEL lab;
    lab.Label.Sid = il;
    lab.Label.Attributes = SE_GROUP_INTEGRITY;
    if (!pSetTokenInformation(tok, TokenIntegrityLevel, &lab, sizeof lab + GetLengthSid(il)))
      say("   SetTokenInformation(integrity) error %lu\n", GetLastError());
    pFreeSid(il);
  }
}

// kind: 1 own token dup, 2 restricted, 3 restricted + low IL, 4 restricted + untrusted IL + restricting SIDs (World),
//       5 Chromium USER_LOCKDOWN-like (deny-only groups, restricting Null SID, low IL), 6 Chromium USER_LIMITED-like
static HANDLE make_token(int kind) {
  if (!pOpenProcessToken || !pDuplicateTokenEx) return NULL;
  HANDLE own = NULL, tok = NULL;
  if (!pOpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &own)) {
    say("   OpenProcessToken error %lu\n", GetLastError());
    return NULL;
  }
  if (kind == 5) {
    tok = make_lockdown_token(own);
    CloseHandle(own);
    return tok;
  }
  if (kind == 6) {
    tok = make_limited_token(own);
    CloseHandle(own);
    return tok;
  }
  if (kind == 1) {
    if (!pDuplicateTokenEx(own, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &tok))
      say("   DuplicateTokenEx error %lu\n", GetLastError());
    CloseHandle(own);
    return tok;
  }
  if (!pCreateRestrictedToken) {
    CloseHandle(own);
    return NULL;
  }
  SID_IDENTIFIER_AUTHORITY world = {SECURITY_WORLD_SID_AUTHORITY};
  PSID everyone = NULL;
  SID_AND_ATTRIBUTES restrict_sids[1];
  DWORD n_restrict = 0;
  if (kind == 4 && pAllocateAndInitializeSid &&
      pAllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone)) {
    restrict_sids[0].Sid = everyone;
    restrict_sids[0].Attributes = 0;
    n_restrict = 1;
  }
  if (!pCreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, n_restrict, n_restrict ? restrict_sids : NULL,
                              &tok)) {
    say("   CreateRestrictedToken error %lu\n", GetLastError());
    tok = NULL;
  }
  if (everyone && pFreeSid) pFreeSid(everyone);
  CloseHandle(own);
  if (tok && (kind == 3 || kind == 4))
    set_integrity(tok, kind == 3 ? SECURITY_MANDATORY_LOW_RID : SECURITY_MANDATORY_UNTRUSTED_RID);
  return tok;
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects the tests share, made once in wmain.
static HANDLE g_dup;                           // own primary token duplicate (CreateProcessAsUserW baseline)
static HANDLE g_job_plain, g_job_limited, g_job_lockdown;
static HANDLE g_event, g_event_noinherit, g_disk_noinherit;
static HANDLE g_real[8];                       // Chromium-like HANDLE_LIST contents, in Chromium's order
static const char* g_real_kind[8];
static int g_nreal;
static HANDLE g_util_event, g_cig[2], g_pipe_server;
static int g_ncig;
static wchar_t g_env[16384];                   // Chromium's filtered environment block
static wchar_t g_desk1[160], g_desk2[160];     // alternate desktop in this window station / Chromium-style alt winsta

static void make_jobs(void) {
  g_job_plain = CreateJobObjectW(NULL, NULL);
  if (g_job_plain) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    ZeroMemory(&li, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job_plain, JobObjectExtendedLimitInformation, &li, sizeof li);
  } else {
    say("CreateJobObjectW error %lu\n", GetLastError());
  }
  // Chromium-like jobs (sandbox/win/src/job.cc): limited-user (GPU) = ACTIVE_PROCESS(1) | KILL_ON_JOB_CLOSE (0x2008), UI
  // restrictions 0; lockdown (renderer, services) = + DIE_ON_UNHANDLED_EXCEPTION (0x2408) and UI restrictions 0xFF.
  for (int k = 0; k < 2; ++k) {
    HANDLE j = CreateJobObjectW(NULL, NULL);
    if (!j) continue;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    ZeroMemory(&li, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                          (k ? JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION : 0);
    li.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li))
      say("SetInformationJobObject(limits, %s) error %lu\n", k ? "lockdown" : "limited", GetLastError());
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui = {k ? 0xFFu : 0u};
    if (!SetInformationJobObject(j, JobObjectBasicUIRestrictions, &ui, sizeof ui))
      say("SetInformationJobObject(UI, %s) error %lu\n", k ? "lockdown" : "limited", GetLastError());
    if (k) g_job_lockdown = j; else g_job_limited = j;
  }
}

static void add_real(HANDLE h, const char* kind) {
  if (h && h != INVALID_HANDLE_VALUE && g_nreal < 8) {
    g_real_kind[g_nreal] = kind;
    g_real[g_nreal++] = h;
  } else {
    say("   (could not make the %s handle: error %lu)\n", kind, GetLastError());
  }
}

// HANDLE_LIST contents as Chromium builds them (spec-b 2.2): metrics section, field-trial section (read-only), pseudonym
// salt section, log-file handle, Mojo channel endpoint; + bootstrap event (utility); + two SEC_IMAGE sections (CIG).
static void make_handles(void) {
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE cur = GetCurrentProcess();
  g_event = CreateEventW(&sa, TRUE, FALSE, NULL);
  g_event_noinherit = CreateEventW(NULL, TRUE, FALSE, NULL);
  add_real(CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 0x40000, NULL), "metrics section");
  HANDLE ft = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x40000, NULL), ftro = NULL;
  if (ft) {
    DuplicateHandle(cur, ft, cur, &ftro, FILE_MAP_READ, TRUE, 0);
    CloseHandle(ft);
  }
  add_real(ftro, "field-trial section (read-only)");
  add_real(CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 0x1000, NULL), "pseudonymization-salt section");
  if (g_log) {
    HANDLE lf = (HANDLE)_get_osfhandle(_fileno(g_log)), d = NULL, nd = NULL;
    if (lf != INVALID_HANDLE_VALUE) {
      DuplicateHandle(cur, lf, cur, &d, 0, TRUE, DUPLICATE_SAME_ACCESS);
      DuplicateHandle(cur, lf, cur, &nd, 0, FALSE, DUPLICATE_SAME_ACCESS);  // spec-b suspect 4: a disk handle not inheritable
    }
    add_real(d, "log file");
    g_disk_noinherit = nd;
  }
  wchar_t pn[96];
  swprintf(pn, 96, L"\\\\.\\pipe\\sbxprobe.%lu.%lu", GetCurrentProcessId(), GetTickCount());
  g_pipe_server = CreateNamedPipeW(pn, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 5000, NULL);
  HANDLE client = INVALID_HANDLE_VALUE;
  if (g_pipe_server != INVALID_HANDLE_VALUE)
    client = CreateFileW(pn, GENERIC_READ | GENERIC_WRITE, 0, &sa, OPEN_EXISTING,
                         SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS | FILE_FLAG_OVERLAPPED, NULL);
  add_real(client, "Mojo channel endpoint (named pipe)");
  HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (ev) {
    DuplicateHandle(cur, ev, cur, &g_util_event, EVENT_MODIFY_STATE, TRUE, 0);
    CloseHandle(ev);
  }
  if (g_image[0]) {
    static const wchar_t* dlls[2] = {L"chrome.dll", L"chrome_elf.dll"};
    for (int i = 0; i < 2; ++i) {
      wchar_t p[MAX_PATH + 32];
      wcscpy(p, g_image);
      wchar_t* s = wcsrchr(p, L'\\');
      if (!s) break;
      wcscpy(s + 1, dlls[i]);
      HANDLE f = CreateFileW(p, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
      HANDLE sec = NULL;
      if (f != INVALID_HANDLE_VALUE) {
        sec = CreateFileMappingW(f, &sa, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
        CloseHandle(f);
      }
      if (sec)
        g_cig[g_ncig++] = sec;
      else
        say("   (SEC_IMAGE section of %ls not made: error %lu)\n", dlls[i], GetLastError());
    }
  }
  say("Chromium-like HANDLE_LIST: %d handles (+ utility event: %s, + %d SEC_IMAGE sections for code integrity)\n", g_nreal,
      g_util_event ? "yes" : "no", g_ncig);
  for (int i = 0; i < g_nreal; ++i) {
    DWORD hf = 0;
    GetHandleInformation(g_real[i], &hf);
    say("   %p %s, file type %lu, inherit %s\n", (void*)g_real[i], g_real_kind[i], GetFileType(g_real[i]),
        (hf & HANDLE_FLAG_INHERIT) ? "yes" : "NO");
  }
}

// Chromium's filtered environment (broker_services.cc, win_utils.cc): only these names, exact case, double-NUL ended.
static void make_env(void) {
  static const wchar_t* keep[] = {L"Path", L"SystemDrive", L"SystemRoot", L"TEMP", L"TMP", L"LOCALAPPDATA",
                                  L"CHROME_CRASHPAD_PIPE_NAME"};
  wchar_t* all = GetEnvironmentStringsW();
  size_t pos = 0, cap = sizeof g_env / sizeof g_env[0];
  int n = 0;
  for (size_t k = 0; k < sizeof keep / sizeof keep[0]; ++k) {
    size_t kl = wcslen(keep[k]);
    for (wchar_t* e = all; e && *e; e += wcslen(e) + 1) {
      if (!wcsncmp(e, keep[k], kl) && e[kl] == L'=') {
        size_t l = wcslen(e) + 1;
        if (pos + l + 2 < cap) {
          memcpy(g_env + pos, e, l * sizeof(wchar_t));
          pos += l;
          ++n;
        }
        break;
      }
    }
  }
  g_env[pos] = 0;
  g_env[pos + 1] = 0;
  if (all) FreeEnvironmentStringsW(all);
  say("filtered environment block (Chromium SetFilterEnvironment): %d variables, %u characters\n", n, (unsigned)pos);
}

// user32 at run time, so the child (the same exe) imports no user32 and can start under WIN32K_SYSTEM_CALL_DISABLE.
typedef HDESK(WINAPI* CreateDesktopW_t)(LPCWSTR, LPCWSTR, void*, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES);
typedef HWINSTA(WINAPI* CreateWindowStationW_t)(LPCWSTR, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES);
typedef HWINSTA(WINAPI* GetProcessWindowStation_t)(void);
typedef BOOL(WINAPI* SetProcessWindowStation_t)(HWINSTA);
typedef BOOL(WINAPI* GetUserObjectInformationW_t)(HANDLE, int, PVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI* CloseDesktop_t)(HDESK);
typedef BOOL(WINAPI* CloseWindowStation_t)(HWINSTA);
static CloseDesktop_t pCloseDesktop;
static CloseWindowStation_t pCloseWindowStation;
static HDESK g_desk1_h, g_desk2_h;
static HWINSTA g_winsta2;

static void make_desktops(void) {
  HMODULE u32 = LoadLibraryW(L"user32.dll");
  if (!u32) {
    say("user32 not loaded (error %lu) - desktop tests skipped\n", GetLastError());
    return;
  }
  CreateDesktopW_t pCreateDesktopW = (CreateDesktopW_t)(void*)GetProcAddress(u32, "CreateDesktopW");
  CreateWindowStationW_t pCreateWindowStationW = (CreateWindowStationW_t)(void*)GetProcAddress(u32, "CreateWindowStationW");
  GetProcessWindowStation_t pGetProcessWindowStation =
      (GetProcessWindowStation_t)(void*)GetProcAddress(u32, "GetProcessWindowStation");
  SetProcessWindowStation_t pSetProcessWindowStation =
      (SetProcessWindowStation_t)(void*)GetProcAddress(u32, "SetProcessWindowStation");
  GetUserObjectInformationW_t pGetUserObjectInformationW =
      (GetUserObjectInformationW_t)(void*)GetProcAddress(u32, "GetUserObjectInformationW");
  pCloseDesktop = (CloseDesktop_t)(void*)GetProcAddress(u32, "CloseDesktop");
  pCloseWindowStation = (CloseWindowStation_t)(void*)GetProcAddress(u32, "CloseWindowStation");
  if (!pCreateDesktopW || !pCreateWindowStationW || !pGetProcessWindowStation || !pSetProcessWindowStation ||
      !pGetUserObjectInformationW || !pCloseDesktop || !pCloseWindowStation) {
    say("user32 functions missing - desktop tests skipped\n");
    return;
  }
  HWINSTA old = pGetProcessWindowStation();
  wchar_t name[64] = L"";
  DWORD needed = 0;
  pGetUserObjectInformationW(old, UOI_NAME, name, sizeof name, &needed);
  // 1: an alternate desktop in this window station
  g_desk1_h = pCreateDesktopW(L"sbxprobe_desktop", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (g_desk1_h)
    swprintf(g_desk1, 160, L"%ls\\sbxprobe_desktop", name);
  else
    say("CreateDesktopW (this window station) error %lu\n", GetLastError());
  // 2: Chromium's (sandbox/win/src/window.cc): a new unnamed window station, a desktop in it, back to the old station
  g_winsta2 = pCreateWindowStationW(NULL, 0, GENERIC_READ | WINSTA_CREATEDESKTOP, NULL);
  if (!g_winsta2 && GetLastError() == ERROR_ACCESS_DENIED)
    g_winsta2 = pCreateWindowStationW(NULL, 0, WINSTA_READATTRIBUTES | WINSTA_CREATEDESKTOP, NULL);
  if (!g_winsta2) {
    say("CreateWindowStationW error %lu - alternate window station tests skipped\n", GetLastError());
  } else if (pSetProcessWindowStation(g_winsta2)) {
    wchar_t dn[64];
    swprintf(dn, 64, L"sbxprobe_alt_desktop_0x%lX", GetCurrentProcessId());
    g_desk2_h = pCreateDesktopW(dn, NULL, NULL, 0,
                                DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | READ_CONTROL | WRITE_DAC | WRITE_OWNER, NULL);
    DWORD e = GetLastError();
    pSetProcessWindowStation(old);
    wchar_t wn[64] = L"";
    pGetUserObjectInformationW(g_winsta2, UOI_NAME, wn, sizeof wn, &needed);
    if (g_desk2_h)
      swprintf(g_desk2, 160, L"%ls\\%ls", wn, dn);
    else
      say("CreateDesktopW (alternate window station) error %lu\n", e);
  } else {
    say("SetProcessWindowStation error %lu - alternate window station tests skipped\n", GetLastError());
  }
  say("desktops: this station \"%ls\", alternate desktop \"%ls\", Chromium-style alternate station \"%ls\"\n", name,
      g_desk1[0] ? g_desk1 : L"(none)", g_desk2[0] ? g_desk2 : L"(none)");
}

// ---------------------------------------------------------------------------------------------------------------------
// Section 4: the Chromium replay and its DIAGNOSIS.
typedef struct {
  DWORD64 v1, v2;
  int mit16;    // pass the mitigation word as 16 bytes even though value2 is 0 (the RS2+ form)
  int mit4;     // v3: pass the low 32 bits of value1 as a DWORD (4 bytes, the older Windows 7 form)
  int child, job, handles, ktm, token, desk, env, image;  // handles: 0 none, 1 one event, 2 Chromium-like set
} Shape;

// returns 1 accepted, 0 refused, -1 skipped (token could not be made)
static int replay_try(const Replay* r, const Shape* s, const char* tag) {
  Attr attrs[6];
  int n = 0;
  DWORD64 mit[2] = {s->v1, s->v2};
  DWORD child = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
  COMPONENT_FILTER cf;
  cf.ComponentFlags = COMPONENT_KTM;
  HANDLE job = s->job == 1 ? g_job_limited : s->job == 2 ? g_job_lockdown : NULL;
  HANDLE jobs[1] = {job};
  HANDLE hl[16];
  int nh = 0;
  if (s->handles == 1 && g_event) hl[nh++] = g_event;
  if (s->handles == 2) {
    for (int i = 0; i < g_nreal; ++i) hl[nh++] = g_real[i];
    if (r->extra == 1 && g_util_event) hl[nh++] = g_util_event;
    if (r->extra == 2)
      for (int i = 0; i < g_ncig; ++i) hl[nh++] = g_cig[i];
  }
#if defined(SBX_INJECT) && SBX_INJECT == 2  // test builds only: a second, independent cause (87 as well)
  if (nh && g_event_noinherit) hl[nh++] = g_event_noinherit;
#endif
  DWORD mit32 = (DWORD)s->v1;
  if (s->mit4) {
    if (mit32) attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &mit32, sizeof mit32, 0);
  } else if (s->v1 || s->v2) {
    attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, mit, (s->v2 || s->mit16) ? 16 : 8, 0);
  }
  if (s->ktm) attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER, &cf, sizeof cf, 1);
  if (s->child) attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &child, sizeof child, 0);
  if (nh) attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, nh * sizeof(HANDLE), 0);
  if (s->job && job) attrs[n++] = mk(PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof jobs, 0);
  HANDLE tok = s->token ? make_token(s->token) : NULL;
  if (s->token && !tok) {
    say("   (%s: token %d could not be made - variant skipped)\n", tag, s->token);
    return -1;
  }
  const wchar_t* desk = s->desk == 2 ? g_desk2 : s->desk == 1 ? g_desk1 : NULL;
  char name[400];
  snprintf(name, sizeof name,
           "REPLAY %s [%s] %s: v1 0x%016llX%s%s%s%s%s token %d%s%s", r->type, s->image ? "venetium.exe" : "probe", tag,
           (unsigned long long)(s->mit4 ? (DWORD)s->v1 : s->v1),
           s->mit4 ? " (4 bytes, older form)" : (s->v2 || s->mit16) ? " (16 bytes)" : "", s->ktm ? " +ktm" : "",
           s->child ? " +child_policy" : "",
           s->handles == 2 ? " +handle_list(Chromium-like)" : s->handles == 1 ? " +handle_list(1 event)" : "",
           s->job == 1 ? " +job_list(limited)" : s->job == 2 ? " +job_list(lockdown)" : "", s->token,
           s->desk == 2 ? " +alt_winstation" : s->desk == 1 ? " +alt_desktop" : "", s->env ? " +filtered_env" : "");
  // Chromium's call shape: CREATE_SUSPENDED|DETACHED_PROCESS|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT
  // (0x0008040C), STARTUPINFO.dwFlags STARTF_FORCEOFFFEEDBACK|STARTF_USESTDHANDLES with NULL std handles.
  Call c = call0();
  c.attrs = attrs;
  c.nattrs = n;
  c.token = tok;
  c.desktop = desk && desk[0] ? desk : NULL;
  c.inherit = nh > 0;
  c.flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
  c.startf = STARTF_FORCEOFFFEEDBACK | STARTF_USESTDHANDLES;
  c.env = s->env ? g_env : NULL;
  c.image_args = s->image ? r->args : NULL;
  c.expect = EXPECT_ANY;  // judged in the DIAGNOSIS block instead
  int acc = run_test(name, &c);
  if (tok) CloseHandle(tok);
  return acc;
}

static const char* kStepName[9] = {"MITIGATION_POLICY", "COMPONENT_FILTER", "CHILD_PROCESS_POLICY", "JOB_LIST",
                                   "HANDLE_LIST", "token", "desktop", "environment", "venetium.exe"};

// cumulative removal step: returns 1 if it took something away
static int apply_step(Shape* t, int step) {
  switch (step) {
    case 0: if (t->v1 || t->v2) { t->v1 = t->v2 = 0; return 1; } break;
    case 1: if (t->ktm) { t->ktm = 0; return 1; } break;
    case 2: if (t->child) { t->child = 0; return 1; } break;
    case 3: if (t->job) { t->job = 0; return 1; } break;
    case 4: if (t->handles) { t->handles = 0; return 1; } break;
    case 5: if (t->token) { t->token = 0; return 1; } break;
    case 6: if (t->desk) { t->desk = 0; return 1; } break;
    case 7: if (t->env) { t->env = 0; return 1; } break;
    case 8: if (t->image) { t->image = 0; return 1; } break;
  }
  return 0;
}

static void append(char* dst, size_t cap, const char* item) {
  size_t n = strlen(dst);
  snprintf(dst + n, cap - n, "%s%s", n ? "; " : "", item);
}

// Chromium's call for this process type, as the replay passes it (image 1 = venetium.exe with Chromium-like handles)
static Shape shape_of(const Replay* r, int image) {
  Shape b;
  ZeroMemory(&b, sizeof b);
  b.v1 = r->requested_v1 & g_supported[0];
  b.v2 = r->requested_v2 & g_supported[1];
  b.child = r->child_process_policy;
  b.job = r->job_list;
  b.handles = r->handle_list ? (image ? 2 : 1) : 0;
  b.ktm = r->component_filter;
  b.token = r->token;
  b.desk = r->desk == 2 && !g_desk2[0] ? (g_desk1[0] ? 1 : 0) : r->desk == 1 && !g_desk1[0] ? 0 : r->desk;
  b.env = r->env;
  b.image = image;
  return b;
}

static void replay_row(const Replay* r, int image, int mit4) {
  Shape b = shape_of(r, image);
  b.mit4 = mit4;
  Buf* out = mit4 ? &g_v3 : &g_diag;
#if defined(SBX_INJECT)  // test builds only: fake a refusal so the leave-one-out and the DIAGNOSIS run on this server
  b.v1 |= 0x2000;        // HEAP_TERMINATE value 3 (reserved): Windows answers 87
#endif
  char label[160];
  snprintf(label, sizeof label, "%s [%s]%s", r->type, image ? "venetium.exe, Chromium-like handles" : "probe exe",
           mit4 ? " 4-byte word" : "");
  if (mit4 && !(DWORD)b.v1) {
    buf_add(out, "%-58s SKIPPED (the low 32 bits of the word are 0 here - no 4-byte attribute to pass)\n", label);
    return;
  }
  say("-- %s: requested v1 0x%016llX & supported 0x%016llX = 0x%016llX\n", label, (unsigned long long)r->requested_v1,
      (unsigned long long)g_supported[0], (unsigned long long)b.v1);
  if (b.desk != r->desk) say("   (desktop %d not available here - using %d)\n", r->desk, b.desk);
  int acc = replay_try(r, &b, mit4 ? "full, 4-byte word" : "full");
  if (acc == 1) {
    buf_add(out, "%-58s ACCEPTED: Windows created it with Chromium's full set%s\n", label,
            image ? " - this control did NOT reproduce an 87 here" : "");
    return;
  }
  if (acc < 0) {
    buf_add(out, "%-58s SKIPPED (token could not be made)\n", label);
    return;
  }
  int how = g_last_result;
  buf_add(out, "%-58s %s%s\n", label, how == 87 ? "REJECTED with 87" : "FAILED (not 87, see the log)",
          image && how == 87 ? " - this control REPRODUCES the device's error" : "");
  say("   %s refused - leave-one-out:\n", label);
  char fixes[2048] = "";
  char tag[120];
#define TRY(TAG, MOD)                                     \
  do {                                                    \
    Shape t = b;                                          \
    MOD;                                                  \
    if (replay_try(r, &t, TAG) == 1) append(fixes, sizeof fixes, TAG); \
  } while (0)
  for (int k = 0; k < 16; ++k) {
    DWORD64 f = k == 0 ? 0xFFull : 0xFull << (4 * k);
    if (k == 1 || !(b.v1 & f) || (mit4 && k >= 8)) continue;
    snprintf(tag, sizeof tag, "without %s", kV1Fields[k]);
    TRY(tag, t.v1 &= ~f);
  }
  if (b.v1 || b.v2) TRY("without MITIGATION_POLICY", (t.v1 = 0, t.v2 = 0));
  if (b.v1 && !b.v2 && !mit4) TRY("MITIGATION_POLICY passed as 16 bytes", t.mit16 = 1);
  if (b.ktm) TRY("without COMPONENT_FILTER", t.ktm = 0);
  if (b.child) TRY("without CHILD_PROCESS_POLICY", t.child = 0);
  if (b.handles) TRY("without HANDLE_LIST", t.handles = 0);
  if (b.handles == 2) TRY("one event in HANDLE_LIST instead of the Chromium-like handles", t.handles = 1);
  if (b.job) TRY("without JOB_LIST", t.job = 0);
  if (b.token >= 4) TRY("token 3 (restricted + low IL) instead", t.token = 3);
  if (b.token >= 3) TRY("token 2 (restricted, own IL) instead", t.token = 2);
  if (b.token >= 2) TRY("token 1 (own token duplicate) instead", t.token = 1);
  if (b.token) TRY("without token (CreateProcessW)", t.token = 0);
  if (b.desk == 2) TRY("alternate desktop in this window station instead", t.desk = 1);
  if (b.desk) TRY("default desktop", t.desk = 0);
  if (b.env) TRY("without filtered environment", t.env = 0);
  if (b.image) TRY("probe exe instead of venetium.exe", t.image = 0);
#undef TRY
  if (fixes[0]) {
    buf_add(out, "   accepted once ONE thing is changed: %s\n", fixes);
    return;
  }
  // No single change helps (two or more causes): take things away cumulatively until Windows accepts, then put each
  // removed item back alone to see which of them are needed together.
  say("   no single change accepted - removing cumulatively:\n");
  Shape t = b;
  char removed[1024] = "";
  int applied[9], na = 0, found = 0;
  for (int step = 0; step < 9 && !found; ++step) {
    if (!apply_step(&t, step)) continue;
    applied[na++] = step;
    append(removed, sizeof removed, kStepName[step]);
    snprintf(tag, sizeof tag, "cumulative: without %.100s", removed);
    if (replay_try(r, &t, tag) == 1) found = 1;
  }
  if (!found) {
    buf_add(out, "   still refused with everything removed (%s) - see sections 0 and 1 (call shape, token)\n",
            removed);
    return;
  }
  char needed[512] = "";
  for (int j = 0; j < na - 1; ++j) {
    Shape u = b;
    for (int i = 0; i < na; ++i)
      if (i != j) apply_step(&u, applied[i]);
    snprintf(tag, sizeof tag, "cumulative check: only %s put back", kStepName[applied[j]]);
    if (replay_try(r, &u, tag) != 1) append(needed, sizeof needed, kStepName[applied[j]]);
  }
  append(needed, sizeof needed, kStepName[applied[na - 1]]);
  buf_add(out, "   no single change helps; accepted after removing %s. Needed together: %s\n", removed, needed);
}

// ---------------------------------------------------------------------------------------------------------------------
static FILE* open_log(const wchar_t* dir, wchar_t* path, size_t cap, const SYSTEMTIME* t) {
  swprintf(path, cap, L"%ls\\sbxprobe-%04u%02u%02u-%02u%02u%02u.log", dir, t->wYear, t->wMonth, t->wDay, t->wHour,
           t->wMinute, t->wSecond);
  FILE* f = _wfopen(path, L"w");
  if (!f) printf("cannot write %ls\n", path);
  return f;
}

static void shape_pair(const char* name, Call c) {
  char nm[320];
  c.token = NULL;
  snprintf(nm, sizeof nm, "%s [CreateProcessW]", name);
  run_test(nm, &c);
  if (g_dup) {
    c.token = g_dup;
    snprintf(nm, sizeof nm, "%s [AsUserW own token dup]", name);
    run_test(nm, &c);
  }
}

static void section2(void) {
  say("\n== 2. MITIGATION_POLICY: one field at a time%s ==\n",
      g_pass_token ? " - again through CreateProcessAsUserW (own token duplicate)" : "");
  int asuser = g_pass_token != NULL;
  if (!asuser) {
    run_mitigation("DEP_ENABLE", 0x01, 0, 8, EXPECT_ANY);
    run_mitigation("DEP_ENABLE|DEP_ATL_THUNK_ENABLE", 0x03, 0, 8, EXPECT_ANY);
    run_mitigation("SEHOP_ENABLE", 0x04, 0, 8, EXPECT_ANY);
  }
  for (int k = 2; k < 16; ++k) {
    int supported = ((g_supported[0] >> (4 * k)) & 0xF) != 0;
    for (int val = 1; val <= 3; ++val) {
      if (asuser && (val == 3 || !supported)) continue;  // the AsUser pass only repeats the rows that matter
      int chromium = val == 1 && ((kChromiumV1 >> (4 * k)) & 0xF) == 1;
      char nm[160];
      snprintf(nm, sizeof nm, "%s %s%s%s", kV1Fields[k], kNibbleVal[val], supported ? "" : " [not in supported mask]",
               chromium ? " [Chromium sets this]" : "");
      run_mitigation(nm, (DWORD64)val << (4 * k), 0, 8, supported && val != 3 ? EXPECT_OK : EXPECT_ANY);
    }
  }
  // Chromium's whole words (masked like Chromium), at 8 bytes, and at 16 bytes (the RS2+ form; informational)
  static const struct { const char* n; DWORD64 w; } words[3] = {
      {"Chromium GPU word", 0x0111000100011000ull},
      {"Chromium renderer/storage word", 0x0111000110011000ull},
      {"Chromium renderer+code-integrity word", 0x0111100110011000ull}};
  for (int i = 0; i < 3; ++i) {
    char nm[160];
    snprintf(nm, sizeof nm, "%s & supported", words[i].n);
    run_mitigation(nm, words[i].w & g_supported[0], 0, 8, EXPECT_OK);
    snprintf(nm, sizeof nm, "%s & supported, passed as 16 bytes (RS2+ form)", words[i].n);
    run_mitigation(nm, words[i].w & g_supported[0], 0, 16, EXPECT_ANY);
  }
  if (!asuser) {
    run_mitigation("value2 = 0 with size 16", 0, 0, 16, EXPECT_ANY);
    for (int k = 1; k < 15; ++k)
      for (int val = 1; val <= 3; ++val) {
        char nm[160];
        snprintf(nm, sizeof nm, "POLICY2 %s %s%s", kV2Fields[k], kNibbleVal[val],
                 ((g_supported[1] >> (4 * k)) & 0xF) ? "" : " [not in supported mask]");
        run_mitigation(nm, 0, (DWORD64)val << (4 * k), 16, EXPECT_ANY);  // Chromium passes no value2 on this OS
      }
  }
  // ALWAYS_ON (1) in every field the OS reports as supported (the mask itself has 3 = both bits per field, which is
  // a reserved value for most fields, so it must not be passed as-is).
  DWORD64 on1 = g_supported[0] & 0x7, on2 = 0;
  for (int k = 2; k < 16; ++k)
    if ((g_supported[0] >> (4 * k)) & 0xF) on1 |= 1ull << (4 * k);
  for (int k = 1; k < 15; ++k)
    if ((g_supported[1] >> (4 * k)) & 0xF) on2 |= 1ull << (4 * k);
  run_mitigation("ALWAYS_ON in every supported value1 field, without DEP/ATL/SEHOP", on1 & ~0xFFull, 0, 8, EXPECT_ANY);
  if (!asuser) {
    run_mitigation("ALWAYS_ON in every supported value1 field (incl. DEP/ATL/SEHOP bits as reported)", on1, 0, 8,
                   EXPECT_ANY);
    if (on2) run_mitigation("ALWAYS_ON in every supported value2 field", 0, on2, 16, EXPECT_ANY);
  }
}

static void section3(void) {
  say("\n== 3. other process attributes, one at a time%s ==\n",
      g_pass_token ? " - again through CreateProcessAsUserW (own token duplicate)" : "");
  if (g_job_limited) {
    HANDLE jobs[1] = {g_job_limited};
    run_attr("JOB_LIST (Chromium-like limited-user job: active process limit 1)",
             mk(PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof jobs, 0), FALSE, EXPECT_OK);
  }
  if (g_job_lockdown) {
    HANDLE jobs[1] = {g_job_lockdown};
    run_attr("JOB_LIST (Chromium-like lockdown job: + die on unhandled exception, UI restrictions)",
             mk(PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof jobs, 0), FALSE, EXPECT_OK);
  }
  if (g_job_plain) {
    HANDLE jobs[1] = {g_job_plain};
    run_attr("JOB_LIST (1 job, KILL_ON_JOB_CLOSE)", mk(PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs, sizeof jobs, 0), FALSE,
             EXPECT_OK);
  }
  {
    DWORD child = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
    run_attr("CHILD_PROCESS_POLICY = RESTRICTED", mk(PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &child, sizeof child, 0),
             FALSE, EXPECT_OK);
  }
  if (g_event) {
    HANDLE hl[1] = {g_event};
    run_attr("HANDLE_LIST (1 inheritable event, bInheritHandles TRUE)",
             mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, sizeof hl, 0), TRUE, EXPECT_OK);
  }
  if (g_nreal) {
    HANDLE hl[8];
    for (int i = 0; i < g_nreal; ++i) hl[i] = g_real[i];
    run_attr("HANDLE_LIST (Chromium-like: 3 sections, log file, Mojo pipe; probe exe)",
             mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, g_nreal * sizeof(HANDLE), 0), TRUE, EXPECT_OK);
  }
  if (g_event_noinherit) {
    HANDLE hl[1] = {g_event_noinherit};
    run_attr("HANDLE_LIST (1 NON-inheritable event - control)",
             mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, sizeof hl, 0), TRUE, EXPECT_87);
  }
  if (g_disk_noinherit) {
    HANDLE hl[1] = {g_disk_noinherit};
    run_attr("HANDLE_LIST (1 NON-inheritable disk file handle, like a redirected stdout - control)",
             mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, sizeof hl, 0), TRUE, EXPECT_87);
  }
  {
    COMPONENT_FILTER cf;
    cf.ComponentFlags = COMPONENT_KTM;
    run_attr("COMPONENT_FILTER = COMPONENT_KTM (raw: Update failing is reported)",
             mk(PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER, &cf, sizeof cf, 0), FALSE, EXPECT_ANY);
    run_attr("COMPONENT_FILTER = COMPONENT_KTM (as Chromium passes it: NOT_SUPPORTED at Update tolerated)",
             mk(PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER, &cf, sizeof cf, 1), FALSE, EXPECT_OK);
  }
  {
    DWORD opt = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
    run_attr("ALL_APPLICATION_PACKAGES_POLICY = OPT_OUT (not used by Chromium here)",
             mk(PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, &opt, sizeof opt, 0), FALSE, EXPECT_ANY);
  }
  Call c = call0();
  c.token = g_pass_token;
  char nm[200];
  c.flags |= CREATE_BREAKAWAY_FROM_JOB;
  c.expect = EXPECT_ANY;
  snprintf(nm, sizeof nm, "CREATE_BREAKAWAY_FROM_JOB flag%s", pass_name());
  run_test(nm, &c);
  c = call0();
  c.token = g_pass_token;
  c.flags |= CREATE_UNICODE_ENVIRONMENT;
  c.env = g_env;
  c.expect = EXPECT_OK;
  snprintf(nm, sizeof nm, "filtered environment block + CREATE_UNICODE_ENVIRONMENT%s", pass_name());
  run_test(nm, &c);
  if (g_desk1[0]) {
    c = call0();
    c.token = g_pass_token;
    c.desktop = g_desk1;
    c.expect = EXPECT_OK;
    snprintf(nm, sizeof nm, "alternate desktop in this window station (lpDesktop = winsta\\desktop)%s", pass_name());
    run_test(nm, &c);
  }
  if (g_desk2[0]) {
    c = call0();
    c.token = g_pass_token;
    c.desktop = g_desk2;
    c.expect = EXPECT_OK;
    snprintf(nm, sizeof nm, "Chromium-style alternate window station + desktop%s", pass_name());
    run_test(nm, &c);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Section 5 (v3): the OLDER form of the mitigation attribute. Windows 7 took a DWORD (4 bytes); Windows 8+ a DWORD64 (8 bytes);
// RS2+ also two DWORD64s (16 bytes). Build 15035 ARM32 refused the 8- and 16-byte forms for every value (DEVICE-3), so Venetium
// 0029 leaves the attribute out on ARM32. If the 4-byte form is accepted AND takes effect, the low 32 bits of Chromium's word
// (fields 0-7: HEAP_TERMINATE, BOTTOM_UP_ASLR, WIN32K_SYSTEM_CALL_DISABLE, ...) could come back at launch (a possible later
// patch). "Accepted" only means Windows created the process, so for four values a --report child reads its own policies back
// and the row says "accepted and effective" or "accepted but ignored" (against a baseline child with no attribute).
static int g_m4_ok, g_m4_rej, g_m4_effective, g_m4_ignored, g_m4_unclear;
static int g_report_base = -1;  // the policy bits of a plain child (no attribute), -1 = unknown

enum { RB_WIN32K_OFF = 1, RB_BOTTOM_UP = 2, RB_FORCE_RELOCATE = 4, RB_STRICT_HANDLES = 8, RB_QUERY_FAILED = 0x8000 };

// --report child: its own policies as bits; exit code CHILD_EXIT_REPORT | bits
static int report_policies(void) {
  int bits = 0;
  HANDLE me = GetCurrentProcess();
  PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY sc;
  PROCESS_MITIGATION_ASLR_POLICY as;
  PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY sh;
  ZeroMemory(&sc, sizeof sc);
  ZeroMemory(&as, sizeof as);
  ZeroMemory(&sh, sizeof sh);
  if (!GetProcessMitigationPolicy(me, ProcessSystemCallDisablePolicy, &sc, sizeof sc)) bits |= RB_QUERY_FAILED;
  if (!GetProcessMitigationPolicy(me, ProcessASLRPolicy, &as, sizeof as)) bits |= RB_QUERY_FAILED;
  if (!GetProcessMitigationPolicy(me, ProcessStrictHandleCheckPolicy, &sh, sizeof sh)) bits |= RB_QUERY_FAILED;
  if (sc.DisallowWin32kSystemCalls) bits |= RB_WIN32K_OFF;
  if (as.EnableBottomUpRandomization) bits |= RB_BOTTOM_UP;
  if (as.EnableForceRelocateImages) bits |= RB_FORCE_RELOCATE;
  if (sh.RaiseExceptionOnInvalidHandleReference) bits |= RB_STRICT_HANDLES;
  return (int)(CHILD_EXIT_REPORT | (DWORD)bits);
}

// check: 0 = creation only; else the policy bit the --report child must show (want_on) or must no longer show (!want_on)
static void run_mit4(const char* name, DWORD v, HANDLE token, DWORD extra_flags, int check, int want_on) {
  DWORD val = v;
  Attr a = mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &val, sizeof val, 0);
  Call c = call0();
  c.attrs = &a;
  c.nattrs = 1;
  c.token = token;
  c.flags |= extra_flags;
  c.expect = EXPECT_ANY;
  if (check) c.child_args = L"--report";
  char full[300];
  snprintf(full, sizeof full, "V3 MITIGATION_POLICY size 4 (older form) value 0x%08lX %s%s%s", (unsigned long)v, name,
           (extra_flags & CREATE_BREAKAWAY_FROM_JOB) ? " +CREATE_BREAKAWAY_FROM_JOB" : "", token ? " [AsUserW]" : "");
  int acc = run_test(full, &c);
  if (extra_flags) {  // the breakaway row is reported on its own line, not in the single-value tally
    buf_add(&g_v3, "4-byte form, value 0x%08lX, with CREATE_BREAKAWAY_FROM_JOB: %s\n", (unsigned long)v,
            acc ? "ACCEPTED" : g_last_result == 87 ? "refused with 87" : "failed (not 87, see the log)");
    return;
  }
  if (!acc) {
    ++g_m4_rej;
    return;
  }
  ++g_m4_ok;
  if (!check) return;
  DWORD ex = g_last_exit;
  const char* res;
  if ((ex & 0xFFFF0000u) != CHILD_EXIT_REPORT || (ex & RB_QUERY_FAILED) || g_report_base < 0) {
    res = "accepted, effect NOT READ (the child could not report, or no baseline)";
    ++g_m4_unclear;
  } else {
    int on = (ex & (DWORD)check) != 0, base_on = (g_report_base & check) != 0;
    if (want_on ? (on && !base_on) : (!on && base_on)) {
      res = "accepted and EFFECTIVE";
      ++g_m4_effective;
    } else if (want_on ? base_on : !base_on) {
      res = "accepted, effect UNCLEAR (a plain child already has this state)";
      ++g_m4_unclear;
    } else {
      res = "accepted but IGNORED (the child's own policy does not show it)";
      ++g_m4_ignored;
    }
  }
  say("   -> %s\n", res);
  buf_add(&g_v3, "4-byte value 0x%08lX %-40s%s: %s\n", (unsigned long)v, name, token ? " [AsUserW]" : "", res);
}

static void section5(void) {
  say("\n== 5. probe v3: the OLDER form - MITIGATION_POLICY as a DWORD (4 bytes) ==\n");
  g_m4_ok = g_m4_rej = g_m4_effective = g_m4_ignored = g_m4_unclear = 0;
  {  // baseline for the effect checks: a plain child reports its own policies
    Call c = call0();
    c.child_args = L"--report";
    c.expect = EXPECT_ANY;
    run_test("V3 baseline: a plain child (no attribute) reports its own policies", &c);
    g_report_base = (g_last_exit & 0xFFFF0000u) == CHILD_EXIT_REPORT && !(g_last_exit & RB_QUERY_FAILED)
                        ? (int)(g_last_exit & 0xFFFF)
                        : -1;
    buf_add(&g_v3, "baseline (plain child): %s0x%X (bits: 1 win32k off, 2 bottom-up ASLR, 4 force relocate, 8 strict handles)\n",
            g_report_base < 0 ? "NOT READ, exit " : "", g_report_base < 0 ? (unsigned)g_last_exit : (unsigned)g_report_base);
  }
  for (int pass = 0; pass < 2; ++pass) {
    HANDLE tok = pass ? g_dup : NULL;
    if (pass && !tok) break;
    run_mit4("(0)", 0, tok, 0, 0, 0);
    run_mit4("DEP_ENABLE (Chromium leaves DEP out on ARM32)", 0x1, tok, 0, 0, 0);
    run_mit4("DEP_ENABLE|DEP_ATL_THUNK_ENABLE", 0x3, tok, 0, 0, 0);
    run_mit4("SEHOP_ENABLE", 0x4, tok, 0, 0, 0);
    for (int k = 2; k < 8; ++k)  // the value1 fields that fit in 32 bits
      for (int val = 1; val <= 2; ++val) {
        int chromium = val == 1 && ((kChromiumV1 >> (4 * k)) & 0xF) == 1;
        int check = 0, want_on = 1;
        if (val == 1 && k == 2) check = RB_FORCE_RELOCATE;
        if (val == 1 && k == 6) check = RB_STRICT_HANDLES;
        if (val == 1 && k == 7) check = RB_WIN32K_OFF;
        if (val == 2 && k == 4) check = RB_BOTTOM_UP, want_on = 0;
        char nm[120];
        snprintf(nm, sizeof nm, "%s %s%s", kV1Fields[k], kNibbleVal[val], chromium ? " [Chromium sets this]" : "");
        run_mit4(nm, (DWORD)val << (4 * k), tok, 0, check, want_on);
      }
    run_mit4("Chromium GPU word, low 32 bits", (DWORD)(0x0111000100011000ull & g_supported[0]), tok, 0, 0, 0);
    run_mit4("Chromium renderer/storage word, low 32 bits", (DWORD)(0x0111000110011000ull & g_supported[0]), tok, 0, 0, 0);
  }
  // Is the inherited job the cause of the 87? (The device ran the probe inside a job; T139 tested the flag alone.)
  run_mit4("(0)", 0, NULL, CREATE_BREAKAWAY_FROM_JOB, 0, 0);
  {
    DWORD64 z[2] = {0, 0};
    Attr a = mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, z, 8, 0);
    Call c = call0();
    c.attrs = &a;
    c.nattrs = 1;
    c.flags |= CREATE_BREAKAWAY_FROM_JOB;
    c.expect = EXPECT_ANY;
    int acc = run_test("V3 MITIGATION_POLICY size 8 value 0 +CREATE_BREAKAWAY_FROM_JOB (is the inherited job the cause?)", &c);
    buf_add(&g_v3, "8-byte form, value 0, with CREATE_BREAKAWAY_FROM_JOB: %s\n",
            acc ? "ACCEPTED - the inherited job matters"
                : g_last_result == 87 ? "refused with 87 - the job is not the cause" : "failed (not 87, see the log)");
  }
  buf_add(&g_v3, "4-byte (older) form, single values: %d accepted, %d refused; effect checks: %d effective, %d ignored, %d unclear\n",
          g_m4_ok, g_m4_rej, g_m4_effective, g_m4_ignored, g_m4_unclear);
  say("-- Chromium's full call with the mitigation word as 4 bytes (low 32 bits); a refused one gets the leave-one-out\n");
  for (size_t i = 0; i < sizeof kReplay / sizeof kReplay[0]; ++i) {
    if (!kReplay[i].args) continue;
    replay_row(&kReplay[i], 0, 1);
    if (g_image[0]) replay_row(&kReplay[i], 1, 1);
  }
}

// Section 6 (v3): the mitigations a sandboxed child applies to ITSELF after startup. On this OS Chromium's delayed set is DLL
// search order, Microsoft-signed binaries only and strict handle checks (+ dynamic-code disable for some process types).
// TargetServicesBase::LowerToken applies it after RevertToSelf, under the lockdown primary token, and PCHECKs each
// SetProcessMitigationPolicy - a failure there kills the child (SetDefaultDllDirectories failing ends it with 7011).
// ApplyProcessMitigationsToCurrentProcess also accepts the launch-only flags; a later patch would have to move them into the
// delayed set to restore them on ARM32. The child (this exe, "--apply N [--revert]") makes the call and exits 42 (applied, read
// back), 41 (returned TRUE, no read-back exists), 43 (read-back does not show it), 0x10000|error (the call failed) or
// 0x20000|error (the read-back failed). Passes: own token; a USER_LIMITED-like token (GPU); a USER_LOCKDOWN-like primary token
// with the main thread impersonating the initial token, and the child calls RevertToSelf first (renderer, storage service).
typedef struct {
  int id;
  const char* name;
  const char* chromium;  // how Chromium uses it on this OS
  int delayed;           // 1 = in Chromium's delayed set here (a failure kills that child after 0029)
} ApplyCase;
static const ApplyCase kApply[] = {
    {1, "HeapSetInformation(HeapEnableTerminationOnCorruption)", "base does this at start in every content process", 0},
    {2, "SetDefaultDllDirectories(SYSTEM32 | USER_DIRS)", "delayed in every sandboxed child (DLL_SEARCH_ORDER)", 1},
    {3, "ProcessASLRPolicy ForceRelocateImages + BottomUp", "Chromium's ASLR call shape (only with RELOCATE_IMAGE)", 0},
    {4, "ProcessStrictHandleCheckPolicy", "delayed in every sandboxed child (STRICT_HANDLE_CHECKS)", 1},
    {5, "ProcessSignaturePolicy MicrosoftSignedOnly", "delayed in every sandboxed child (FORCE_MS_SIGNED_BINS)", 1},
    {6, "ProcessDynamicCodePolicy ProhibitDynamicCode", "delayed where Chromium asks (DYNAMIC_CODE_DISABLE)", 1},
    {7, "ProcessExtensionPointDisablePolicy", "launch-only; never applied after 0029 (EXTENSION_POINT_DISABLE)", 0},
    {8, "ProcessFontDisablePolicy DisableNonSystemFonts", "launch-only; never applied after 0029 (FONT_DISABLE)", 0},
    {9, "ProcessImageLoadPolicy NoRemote + NoLowMandatoryLabel", "launch-only; never applied after 0029 (IMAGE_LOAD_*)", 0},
    {10, "ProcessSystemCallDisablePolicy DisallowWin32kSystemCalls", "launch-only (renderer, storage); never applied after 0029", 0},
#if defined(SBX_INJECT)  // test builds only: a call that always fails (unknown id -> ERROR_INVALID_PARAMETER in the child)
    {99, "INJECTED failing call", "test only", 1},
#endif
};

#define SETPOL(POLICY, STRUCT, FIELDS, CHECK)                                               \
  do {                                                                                      \
    STRUCT p;                                                                               \
    ZeroMemory(&p, sizeof p);                                                               \
    FIELDS;                                                                                 \
    if (!SetProcessMitigationPolicy(POLICY, &p, sizeof p))                                  \
      return (int)(CHILD_EXIT_CALL_FAILED | (GetLastError() & 0xFFFF));                     \
    ZeroMemory(&p, sizeof p);                                                               \
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), POLICY, &p, sizeof p))             \
      return (int)(CHILD_EXIT_READBACK_FAILED | (GetLastError() & 0xFFFF));                 \
    return (CHECK) ? CHILD_EXIT : CHILD_EXIT_NOT_VISIBLE;                                   \
  } while (0)

static int apply_policy(int id, int revert) {
  if (revert) {  // like LowerToken: drop the initial (impersonation) token, continue under the lockdown primary token
    typedef BOOL(WINAPI * RevertToSelf_t)(void);
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");
    RevertToSelf_t f = adv ? (RevertToSelf_t)(void*)GetProcAddress(adv, "RevertToSelf") : NULL;
    if (!f || !f()) return (int)(CHILD_EXIT_CALL_FAILED | ((f ? GetLastError() : ERROR_PROC_NOT_FOUND) & 0xFFFF));
  }
  switch (id) {
    case 1:
      if (!HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0))
        return (int)(CHILD_EXIT_CALL_FAILED | (GetLastError() & 0xFFFF));
      return CHILD_EXIT_NO_READBACK;
    case 2: {
      typedef BOOL(WINAPI * SetDefaultDllDirectories_t)(DWORD);
      SetDefaultDllDirectories_t f = (SetDefaultDllDirectories_t)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                       "SetDefaultDllDirectories");
      if (!f) return (int)(CHILD_EXIT_CALL_FAILED | ERROR_PROC_NOT_FOUND);
      if (!f(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS))
        return (int)(CHILD_EXIT_CALL_FAILED | (GetLastError() & 0xFFFF));
      return CHILD_EXIT_NO_READBACK;
    }
    case 3:
      SETPOL(ProcessASLRPolicy, PROCESS_MITIGATION_ASLR_POLICY,
             (p.EnableForceRelocateImages = 1, p.EnableBottomUpRandomization = 1), p.EnableForceRelocateImages);
    case 4:
      SETPOL(ProcessStrictHandleCheckPolicy, PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY,
             (p.RaiseExceptionOnInvalidHandleReference = 1, p.HandleExceptionsPermanentlyEnabled = 1),
             p.RaiseExceptionOnInvalidHandleReference);
    case 5:
      SETPOL(ProcessSignaturePolicy, PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY, p.MicrosoftSignedOnly = 1,
             p.MicrosoftSignedOnly);
    case 6:
      SETPOL(ProcessDynamicCodePolicy, PROCESS_MITIGATION_DYNAMIC_CODE_POLICY, p.ProhibitDynamicCode = 1,
             p.ProhibitDynamicCode);
    case 7:
      SETPOL(ProcessExtensionPointDisablePolicy, PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY,
             p.DisableExtensionPoints = 1, p.DisableExtensionPoints);
    case 8:
      SETPOL(ProcessFontDisablePolicy, PROCESS_MITIGATION_FONT_DISABLE_POLICY, p.DisableNonSystemFonts = 1,
             p.DisableNonSystemFonts);
    case 9:
      SETPOL(ProcessImageLoadPolicy, PROCESS_MITIGATION_IMAGE_LOAD_POLICY,
             (p.NoRemoteImages = 1, p.NoLowMandatoryLabelImages = 1), p.NoRemoteImages && p.NoLowMandatoryLabelImages);
    case 10:
      SETPOL(ProcessSystemCallDisablePolicy, PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY, p.DisallowWin32kSystemCalls = 1,
             p.DisallowWin32kSystemCalls);
    default:
      return (int)(CHILD_EXIT_CALL_FAILED | ERROR_INVALID_PARAMETER);
  }
}
#undef SETPOL

static void section6(void) {
  say("\n== 6. probe v3: mitigations a child applies to ITSELF after startup (Chromium's delayed calls; PCHECKed there) ==\n");
  HANDLE limited = make_token(6), lockdown = make_token(5), imp = NULL;
  // Chromium's initial token is a restricted sibling of the lockdown token with the same low IL; the USER_LIMITED-like token,
  // as an impersonation token, plays that part here.
  if (limited && pDuplicateTokenEx)
    pDuplicateTokenEx(limited, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &imp);
  if (!imp || !pSetThreadToken) {
    say("   (no impersonation token or SetThreadToken - the lockdown pass is skipped)\n");
    if (lockdown) CloseHandle(lockdown);
    lockdown = NULL;
  }
  int bad_delayed = 0, bad_other = 0;
  for (size_t i = 0; i < sizeof kApply / sizeof kApply[0]; ++i) {
    char res[3][120];
    for (int pass = 0; pass < 3; ++pass) {
      HANDLE tok = pass == 1 ? limited : pass == 2 ? lockdown : NULL;
      snprintf(res[pass], sizeof res[pass], "-");
      if (pass && !tok) continue;
      wchar_t args[40];
      swprintf(args, 40, L"--apply %d%ls", kApply[i].id, pass == 2 ? L" --revert" : L"");
      Call c = call0();
      c.token = tok;
      c.thread_token = pass == 2 ? imp : NULL;
      c.child_args = args;
      c.expect = EXPECT_ANY;
      char nm[300];
      snprintf(nm, sizeof nm, "V3 child applies %s [%s]%s", kApply[i].name, kApply[i].chromium,
               pass == 1   ? " [AsUserW USER_LIMITED-like token]"
               : pass == 2 ? " [AsUserW USER_LOCKDOWN-like token, initial token, RevertToSelf]"
                           : "");
      run_test(nm, &c);
      int good = g_last_result == 0 && (!strcmp(g_last_verdict, "OK") || !strcmp(g_last_verdict, "OK-NO-READBACK"));
      if (good)
        snprintf(res[pass], sizeof res[pass], "%s", g_last_verdict);
      else
        snprintf(res[pass], sizeof res[pass], "%s (%.80s)", g_last_result ? "LAUNCH REFUSED" : g_last_verdict,
                 g_last_detail);
      if (!good) {
        if (kApply[i].delayed) ++bad_delayed; else ++bad_other;
      }
    }
    buf_add(&g_v3, "in-child %s%s\n   own: %s | limited: %s | lockdown: %s\n", kApply[i].name,
            kApply[i].delayed ? " [Chromium's delayed set]" : "", res[0], res[1], res[2]);
  }
  buf_add(&g_v3, "in-child summary: %d failed runs of Chromium's delayed calls%s; %d failed runs of the other calls\n",
          bad_delayed, bad_delayed ? " - THOSE CHILDREN WOULD BE KILLED BY CHROMIUM'S PCHECK AFTER 0029" : "", bad_other);
  if (limited) CloseHandle(limited);
  if (lockdown) CloseHandle(lockdown);
  if (imp) CloseHandle(imp);
}

// ---------------------------------------------------------------------------------------------------------------------
int wmain(int argc, wchar_t** argv) {
  // No hard-error or WER dialogs for this process or its children (they inherit the mode): a child that fails in the
  // loader must end at once with its status, not wait on a message box.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  if (argc > 1 && !wcscmp(argv[1], L"--child")) return CHILD_EXIT;
  if (argc > 2 && !wcscmp(argv[1], L"--apply"))  // section 6 child
    return apply_policy(_wtoi(argv[2]), argc > 3 && !wcscmp(argv[3], L"--revert"));
  if (argc > 1 && !wcscmp(argv[1], L"--report")) return report_policies();  // section 5 child
  setlocale(LC_ALL, ".UTF-8");  // %ls with non-ASCII characters (a Persian user name in the path) must not cut lines
  // A tap or click in the console starts a QuickEdit selection, which blocks the next write until Esc: switch QuickEdit off
  // for the run (this console window only) and restore it at the end.
  HANDLE con_in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD con_mode = 0;
  BOOL con_ok = con_in && con_in != INVALID_HANDLE_VALUE && GetConsoleMode(con_in, &con_mode);
  if (con_ok) SetConsoleMode(con_in, (con_mode | ENABLE_EXTENDED_FLAGS) & ~(DWORD)ENABLE_QUICK_EDIT_MODE);

  GetModuleFileNameW(NULL, g_exe, MAX_PATH);
  wchar_t dir[MAX_PATH];
  wcscpy(dir, g_exe);
  wchar_t* slash = wcsrchr(dir, L'\\');
  if (slash) *slash = 0;
  {  // venetium.exe next to the probe, else one folder up (the README layout: <browser folder>\diag\sbxprobe.exe), else
     // two folders up (the zip extracted with an extra folder around diag)
    wchar_t up[MAX_PATH + 32];
    wcscpy(up, dir);
    for (int level = 0; level < 3 && !g_image[0]; ++level) {
      size_t n = wcslen(up);
      if (n + 14 >= MAX_PATH) break;
      wcscpy(up + n, L"\\venetium.exe");
      DWORD at = GetFileAttributesW(up);
      if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) wcscpy(g_image, up);
      up[n] = 0;
      wchar_t* s2 = wcsrchr(up, L'\\');
      if (!s2) break;
      *s2 = 0;
    }
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t logpath[2 * MAX_PATH];
  if (argc > 1) g_log = open_log(argv[1], logpath, sizeof logpath / sizeof logpath[0], &t);
  if (!g_log) g_log = open_log(dir, logpath, sizeof logpath / sizeof logpath[0], &t);
  if (!g_log) {
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n && n < MAX_PATH) {
      if (tmp[n - 1] == L'\\') tmp[n - 1] = 0;
      g_log = open_log(tmp, logpath, sizeof logpath / sizeof logpath[0], &t);
    }
  }
  if (!g_log) {
    wchar_t cwd[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, cwd)) g_log = open_log(cwd, logpath, sizeof logpath / sizeof logpath[0], &t);
  }
  if (!g_log) printf("NO LOG FILE could be written - console only\n");
  buf_init(&g_unexp, 256 * 1024);
  buf_init(&g_diag, 256 * 1024);
  buf_init(&g_v3, 64 * 1024);

  environment();
  if (g_log) say("log file: %ls\n", logpath);
  load_advapi();
  g_dup = make_token(1);
  make_jobs();
  make_handles();
  make_env();
  make_desktops();

  // Section 0: the call shape without risky attributes, through both entry points
  say("\n== 0. call shape (creation flags, STARTF, the working network-service shape), CreateProcessW and AsUserW ==\n");
  {
    Call c = call0();
    c.expect = EXPECT_OK;
    c.flags = 0;
    shape_pair("flags 0, no attributes", c);
    c.flags = CREATE_SUSPENDED;
    shape_pair("CREATE_SUSPENDED", c);
    c.flags = DETACHED_PROCESS;
    shape_pair("DETACHED_PROCESS", c);
    c.flags = CREATE_SUSPENDED | DETACHED_PROCESS;
    shape_pair("CREATE_SUSPENDED|DETACHED_PROCESS (what sections 1-3 use)", c);
    c.flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    shape_pair("0x40C = + CREATE_UNICODE_ENVIRONMENT (NULL environment)", c);
    c.flags = 0;
    c.startf = STARTF_FORCEOFFFEEDBACK | STARTF_USESTDHANDLES;
    shape_pair("STARTF_FORCEOFFFEEDBACK|STARTF_USESTDHANDLES with NULL std handles, flags 0", c);
    if (g_event) {
      HANDLE hl[1] = {g_event};
      Attr a = mk(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, sizeof hl, 0);
      c = call0();
      c.expect = EXPECT_OK;
      c.attrs = &a;
      c.nattrs = 1;
      c.inherit = TRUE;
      c.flags = 0;
      shape_pair("network-service shape (works on the device): EXTENDED_STARTUPINFO + HANDLE_LIST{1 event}", c);
      c.flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
      c.startf = STARTF_FORCEOFFFEEDBACK | STARTF_USESTDHANDLES;
      shape_pair("Chromium flags 0x0008040C + STARTF 0x180 + HANDLE_LIST{1 event}", c);
      c.env = g_env;
      shape_pair("Chromium flags 0x0008040C + STARTF 0x180 + HANDLE_LIST{1 event} + filtered environment", c);
      if (g_image[0]) {
        c.env = NULL;
        c.image_args = L"--type=gpu-process";
        shape_pair("venetium.exe (created, never run): Chromium flags + STARTF + HANDLE_LIST{1 event}", c);
        c.attrs = NULL;
        c.nattrs = 0;
        c.inherit = FALSE;
        c.flags = CREATE_SUSPENDED;
        c.startf = 0;
        shape_pair("venetium.exe (created, never run): CREATE_SUSPENDED, no attributes", c);
      }
    }
  }

  // Section 1: baselines, token ladder
  say("\n== 1. baselines and tokens ==\n");
  {
    Call c = call0();
    c.expect = EXPECT_OK;
    run_test("plain CreateProcessW", &c);
    DWORD64 zero[1] = {0};
    Attr a = mk(PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, zero, 8, 0);
    c.attrs = &a;
    c.nattrs = 1;
    run_test("EXTENDED_STARTUPINFO + MITIGATION_POLICY = 0 (8 bytes)", &c);
  }
  for (int kind = 1; kind <= 6; ++kind) {
    static const char* names[] = {"", "CreateProcessAsUserW own token (duplicate)",
                                  "CreateProcessAsUserW restricted token (DISABLE_MAX_PRIVILEGE)",
                                  "CreateProcessAsUserW restricted token + low integrity",
                                  "CreateProcessAsUserW restricted token + untrusted integrity + restricting SID World",
                                  "CreateProcessAsUserW Chromium USER_LOCKDOWN-like token (child cannot run: expected)",
                                  "CreateProcessAsUserW Chromium USER_LIMITED-like token"};
    HANDLE tok = make_token(kind);
    if (!tok) {
      say("T--- (skipped, no token) %s\n", names[kind]);
      continue;
    }
    Call c = call0();
    c.token = tok;
    c.expect = EXPECT_OK;
    run_test(names[kind], &c);
    CloseHandle(tok);
  }

  // Sections 2 and 3: single factors through CreateProcessW, then again through CreateProcessAsUserW (own token dup)
  g_pass_token = NULL;
  section2();
  section3();
  if (g_dup) {
    g_pass_token = g_dup;
    section2();
    section3();
    g_pass_token = NULL;
  }

  // Section 4: Chromium replay, first with the probe as the child, then the real-image control
  say("\n== 4. Chromium replay (what the sandbox passes for each process type on this OS, masked like Chromium) ==\n");
  for (size_t i = 0; i < sizeof kReplay / sizeof kReplay[0]; ++i) replay_row(&kReplay[i], 0, 0);
  if (g_image[0]) {
    say("\n== 4b. real-image control: venetium.exe (created suspended, never run) with Chromium-like handles ==\n");
    for (size_t i = 0; i < sizeof kReplay / sizeof kReplay[0]; ++i)
      if (kReplay[i].args) replay_row(&kReplay[i], 1, 0);
  } else {
    buf_add(&g_diag, "real-image control: SKIPPED - venetium.exe not found next to the probe or one or two folders up\n");
  }

  // Sections 5 and 6 (v3): measurements for a possible 0030 and for the next failure after 0029
  section5();
  section6();

  if (g_desk1_h) pCloseDesktop(g_desk1_h);
  if (g_desk2_h) pCloseDesktop(g_desk2_h);
  if (g_winsta2) pCloseWindowStation(g_winsta2);
  HANDLE closers[] = {g_dup, g_job_plain, g_job_limited, g_job_lockdown, g_event, g_event_noinherit, g_disk_noinherit,
                      g_util_event, g_cig[0], g_cig[1]};
  for (size_t i = 0; i < sizeof closers / sizeof closers[0]; ++i)
    if (closers[i]) CloseHandle(closers[i]);
  for (int i = 0; i < g_nreal; ++i) CloseHandle(g_real[i]);
  if (g_pipe_server && g_pipe_server != INVALID_HANDLE_VALUE) CloseHandle(g_pipe_server);

  say("\n== SUMMARY: %d tests: %d created (%d ran normally, %d created but the child then failed), %d rejected with 87, "
      "%d other failures; %d UNEXPECTED ==\n",
      g_tests, g_created, g_ran_ok, g_child_failed, g_rej87, g_other, g_unexpected);
  say("UNEXPECTED (should have been accepted but was refused, or a control that should be refused was accepted):\n%s",
      g_unexp.len ? g_unexp.p : "  (none)\n");
  say("\n== DIAGNOSIS (Chromium replay; 'accepted once ONE thing is changed' names the culprit) ==\n%s",
      g_diag.len ? g_diag.p : "  (no replay rows)\n");
  say("\n== PROBE v3 (older 4-byte form; mitigations a child applies to itself - Chromium PCHECKs those calls) ==\n%s",
      g_v3.len ? g_v3.p : "  (none)\n");
  if (g_log)
    say("\nlog: %ls\n", logpath);
  else
    say("\nNO LOG FILE - copy the text of this window instead\n");
  if (g_log) fclose(g_log);
  if (con_ok) SetConsoleMode(con_in, con_mode | ENABLE_EXTENDED_FLAGS);
  return 0;
}
