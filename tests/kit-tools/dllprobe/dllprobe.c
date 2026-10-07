// dllprobe - BATCH-SBX-1. Why does the sandboxed RENDERER fail to load chrome.dll on Windows 10 15035 ARM32 (0xC0000135 /
// ERROR_MOD_NOT_FOUND 0x7E, chrome\app\main_dll_loader_win.cc:141) while the GPU process loads it fine in the same run?
// The probe only MEASURES; it applies no fix and changes nothing on the machine. It writes dllprobe-<date>-<time>.txt next to
// itself. The real answer is his device run; the x64 build here is the server instrument check.
//
// It reproduces MainDllLoader's load (SetCurrentDirectoryW(dir) + LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
// under each sandbox token shape and settles the four candidate causes:
//   H1 label/ACL - the restricted token/low integrity cannot READ chrome.dll or the app folder (read-backs show ACCESS_DENIED).
//   H2 dependency - chrome.dll itself loads/reads, but a dependency DLL is not found (loader snaps name it).
//   H3/H4 order/token - the load succeeds BEFORE the token drop (still impersonating the broker's initial token) and fails AFTER
//                       it drops to the restricted primary token (RevertToSelf) - then the fix is ordering, not ACLs.
//
// Token shapes reuse sbxprobe's builders verbatim (make_lockdown_token = USER_LOCKDOWN-like, make_limited_token = USER_LIMITED-like,
// set_integrity, load_advapi, make_token) from venetium\tests\kit-tools\sbxprobe\sbxprobe.c (BATCH-ARM-FIX-2 v3.1).
//
// Modes:
//   dllprobe.exe [--dll <chrome.dll path>]      run the matrix (default: chrome.dll next to the exe)
//   dllprobe.exe --load <kind> <when> <snaps> <dllpath>   internal child: load under one regime and report via OutputDebugString
//     kind  = the child's PRIMARY token (set by the parent): 1 own, 5 lockdown, 6 limited (echoed)
//     when  = plain | before | after   (before: load while impersonating the initial token, then RevertToSelf; after: RevertToSelf
//             to the restricted primary token first, then load - the H3/H4 order)
//     snaps = 0 | 1   (1: set FLG_SHOW_LDR_SNAPS in the child PEB before the load, so the loader names the missing module)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef LOAD_WITH_ALTERED_SEARCH_PATH
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008
#endif
#define FLG_SHOW_LDR_SNAPS 0x00000002

// ---------------------------------------------------------------------------------------------------------------------
static FILE* g_log;
static void say(const char* fmt, ...) {
  char buf[4096];
  va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  if (n < 0) return;
  fwrite(buf, 1, (size_t)n, stdout); fflush(stdout);
  if (g_log) { fwrite(buf, 1, (size_t)n, g_log); fflush(g_log); }
}
static const char* errname(DWORD e) {
  switch (e) {
    case 0: return "OK";
    case ERROR_FILE_NOT_FOUND: return "FILE_NOT_FOUND(2)";
    case ERROR_PATH_NOT_FOUND: return "PATH_NOT_FOUND(3)";
    case ERROR_ACCESS_DENIED: return "ACCESS_DENIED(5)";
    case ERROR_NOT_ENOUGH_MEMORY: return "NOT_ENOUGH_MEMORY(8)";
    case ERROR_BAD_EXE_FORMAT: return "BAD_EXE_FORMAT(193)";
    case ERROR_MOD_NOT_FOUND: return "MOD_NOT_FOUND(126)";
    case ERROR_PROC_NOT_FOUND: return "PROC_NOT_FOUND(127)";
    case ERROR_INVALID_IMAGE_HASH: return "INVALID_IMAGE_HASH(577)";
    case ERROR_DLL_INIT_FAILED: return "DLL_INIT_FAILED(1114)";
    case 0xC0000005: return "STATUS_ACCESS_VIOLATION";
    case 0xC0000135: return "STATUS_DLL_NOT_FOUND";
    case 0xC0000139: return "STATUS_ENTRYPOINT_NOT_FOUND";
    case 0xC0000142: return "STATUS_DLL_INIT_FAILED";
    case 0xC0000022: return "STATUS_ACCESS_DENIED";
    default: return "(other)";
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Token builders - reused from sbxprobe.c (BATCH-ARM-FIX-2 §6, v3.1).
typedef BOOL(WINAPI* CreateProcessAsUserW_t)(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL,
                                             DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef BOOL(WINAPI* CreateRestrictedToken_t)(HANDLE, DWORD, DWORD, PSID_AND_ATTRIBUTES, DWORD, PLUID_AND_ATTRIBUTES,
                                              DWORD, PSID_AND_ATTRIBUTES, PHANDLE);
typedef BOOL(WINAPI* OpenProcessToken_t)(HANDLE, DWORD, PHANDLE);
typedef BOOL(WINAPI* DuplicateTokenEx_t)(HANDLE, DWORD, LPSECURITY_ATTRIBUTES, SECURITY_IMPERSONATION_LEVEL, TOKEN_TYPE, PHANDLE);
typedef BOOL(WINAPI* SetTokenInformation_t)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD);
typedef BOOL(WINAPI* AllocateAndInitializeSid_t)(PSID_IDENTIFIER_AUTHORITY, BYTE, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                 DWORD, DWORD, PSID*);
typedef PVOID(WINAPI* FreeSid_t)(PSID);
typedef BOOL(WINAPI* GetTokenInformation_t)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
typedef BOOL(WINAPI* SetThreadToken_t)(PHANDLE, HANDLE);
static CreateProcessAsUserW_t pCreateProcessAsUserW;
static CreateRestrictedToken_t pCreateRestrictedToken;
static OpenProcessToken_t pOpenProcessToken;
static DuplicateTokenEx_t pDuplicateTokenEx;
static SetTokenInformation_t pSetTokenInformation;
static AllocateAndInitializeSid_t pAllocateAndInitializeSid;
static FreeSid_t pFreeSid;
static GetTokenInformation_t pGetTokenInformation;
static SetThreadToken_t pSetThreadToken;

static void load_advapi(void) {
  HMODULE adv = LoadLibraryW(L"advapi32.dll");
  CreateProcessAsUserW_t a = adv ? (CreateProcessAsUserW_t)(void*)GetProcAddress(adv, "CreateProcessAsUserW") : NULL;
  pCreateProcessAsUserW = a ? a : (CreateProcessAsUserW_t)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateProcessAsUserW");
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
}

static void set_integrity(HANDLE tok, DWORD rid) {
  SID_IDENTIFIER_AUTHORITY ml = {SECURITY_MANDATORY_LABEL_AUTHORITY};
  PSID il = NULL;
  if (pAllocateAndInitializeSid && pSetTokenInformation && pAllocateAndInitializeSid(&ml, 1, rid, 0, 0, 0, 0, 0, 0, 0, &il)) {
    TOKEN_MANDATORY_LABEL lab;
    lab.Label.Sid = il;
    lab.Label.Attributes = SE_GROUP_INTEGRITY;
    pSetTokenInformation(tok, TokenIntegrityLevel, &lab, sizeof lab + GetLengthSid(il));
    pFreeSid(il);
  }
}

static HANDLE make_lockdown_token(HANDLE own) {
  if (!pGetTokenInformation || !pCreateRestrictedToken || !pAllocateAndInitializeSid) return NULL;
  DWORD len = 0;
  pGetTokenInformation(own, TokenGroups, NULL, 0, &len);
  TOKEN_GROUPS* g = (TOKEN_GROUPS*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len ? len : 1);
  BYTE userbuf[256]; DWORD ulen = 0;
  if (!g || !pGetTokenInformation(own, TokenGroups, g, len, &len) ||
      !pGetTokenInformation(own, TokenUser, userbuf, sizeof userbuf, &ulen)) { if (g) HeapFree(GetProcessHeap(), 0, g); return NULL; }
  SID_AND_ATTRIBUTES* deny = (SID_AND_ATTRIBUTES*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SID_AND_ATTRIBUTES) * (g->GroupCount + 1));
  DWORD nd = 0;
  deny[nd++].Sid = ((TOKEN_USER*)userbuf)->User.Sid;
  for (DWORD i = 0; i < g->GroupCount; ++i) {
    if (g->Groups[i].Attributes & (SE_GROUP_INTEGRITY | SE_GROUP_LOGON_ID)) continue;
    deny[nd++].Sid = g->Groups[i].Sid;
  }
  SID_IDENTIFIER_AUTHORITY nullauth = {SECURITY_NULL_SID_AUTHORITY};
  PSID nullsid = NULL;
  pAllocateAndInitializeSid(&nullauth, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &nullsid);
  SID_AND_ATTRIBUTES restrict_sids[1] = {{nullsid, 0}};
  HANDLE tok = NULL;
  if (!pCreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, nd, deny, 0, NULL, nullsid ? 1 : 0, nullsid ? restrict_sids : NULL, &tok)) tok = NULL;
  if (tok) set_integrity(tok, SECURITY_MANDATORY_LOW_RID);
  if (nullsid) pFreeSid(nullsid);
  HeapFree(GetProcessHeap(), 0, deny); HeapFree(GetProcessHeap(), 0, g);
  return tok;
}

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
  BYTE userbuf[256]; DWORD ulen = 0;
  if (!g || !pGetTokenInformation(own, TokenGroups, g, len, &len) ||
      !pGetTokenInformation(own, TokenUser, userbuf, sizeof userbuf, &ulen)) { if (g) HeapFree(GetProcessHeap(), 0, g); return NULL; }
  SID_AND_ATTRIBUTES* deny = (SID_AND_ATTRIBUTES*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SID_AND_ATTRIBUTES) * (g->GroupCount + 1));
  SID_AND_ATTRIBUTES restrict_sids[4];
  DWORD nd = 0, nr = 0;
  PSID logon = NULL;
  for (DWORD i = 0; i < g->GroupCount; ++i) {
    PSID s = g->Groups[i].Sid;
    if (g->Groups[i].Attributes & SE_GROUP_INTEGRITY) continue;
    if (g->Groups[i].Attributes & SE_GROUP_LOGON_ID) { logon = s; continue; }
    if (sid_is(s, 5, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 2) || sid_is(s, 1, SECURITY_WORLD_RID, 0, 1) ||
        sid_is(s, 5, SECURITY_INTERACTIVE_RID, 0, 1)) continue;
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
  if (!pCreateRestrictedToken(own, DISABLE_MAX_PRIVILEGE, nd, deny, 0, NULL, nr, restrict_sids, &tok)) tok = NULL;
  if (tok) set_integrity(tok, SECURITY_MANDATORY_LOW_RID);
  if (users) pFreeSid(users);
  if (everyone) pFreeSid(everyone);
  if (restricted) pFreeSid(restricted);
  HeapFree(GetProcessHeap(), 0, deny); HeapFree(GetProcessHeap(), 0, g);
  return tok;
}

// kind: 1 own-token duplicate (primary), 5 USER_LOCKDOWN-like, 6 USER_LIMITED-like
static HANDLE make_token(int kind) {
  if (!pOpenProcessToken || !pDuplicateTokenEx) return NULL;
  HANDLE own = NULL, tok = NULL;
  if (!pOpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &own)) return NULL;
  if (kind == 5) tok = make_lockdown_token(own);
  else if (kind == 6) tok = make_limited_token(own);
  else if (kind == 1) { if (!pDuplicateTokenEx(own, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &tok)) tok = NULL; }
  CloseHandle(own);
  return tok;
}

// the broker's INITIAL token: an impersonation duplicate of our own token, set on the child's main thread before it runs
static HANDLE make_initial_impersonation(void) {
  if (!pOpenProcessToken || !pDuplicateTokenEx) return NULL;
  HANDLE own = NULL, imp = NULL;
  if (!pOpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &own)) return NULL;
  if (!pDuplicateTokenEx(own, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &imp)) imp = NULL;
  CloseHandle(own);
  return imp;
}

// ---------------------------------------------------------------------------------------------------------------------
// FLG_SHOW_LDR_SNAPS in the child's own PEB (arch-portable via RtlGetCurrentPeb + the known NtGlobalFlag offset).
typedef PVOID(WINAPI* RtlGetCurrentPeb_t)(void);
static int set_ldr_snaps(void) {
  RtlGetCurrentPeb_t f = (RtlGetCurrentPeb_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetCurrentPeb");
  if (!f) return 0;
  BYTE* peb = (BYTE*)f();
  if (!peb) return 0;
#if defined(_M_X64) || defined(_M_ARM64)
  DWORD* ngf = (DWORD*)(peb + 0xBC);
#else  // _M_IX86 / _M_ARM (32-bit PEB)
  DWORD* ngf = (DWORD*)(peb + 0x68);
#endif
  __try { *ngf |= FLG_SHOW_LDR_SNAPS; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
  return 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Child: load chrome.dll under one regime, report every step via OutputDebugStringA ("DP|..."), which the parent (debugging it)
// captures. Returns the load's GetLastError (0 on success) so the parent has the answer even without the debug strings.
static void dbg(const char* fmt, ...) {
  char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  OutputDebugStringA(b);
}
static const char* rb_open(const wchar_t* path) {
  HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); return e == ERROR_ACCESS_DENIED ? "ACCESS_DENIED" : (e == ERROR_FILE_NOT_FOUND ? "FILE_NOT_FOUND" : "OTHER"); }
  // read the first page while we hold it open
  BYTE pg[4096]; DWORD got = 0; BOOL ok = ReadFile(h, pg, sizeof pg, &got, NULL);
  CloseHandle(h);
  return ok && got > 0 ? "OK" : "OPEN_OK_READ_FAIL";
}

static int do_load_child(int kind, const char* when, int snaps, const wchar_t* dllpath) {
  dbg("DP|mode|kind=%d when=%s snaps=%d", kind, when, snaps);
  // the app folder
  wchar_t dir[MAX_PATH]; wcsncpy(dir, dllpath, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
  wchar_t* sl = wcsrchr(dir, L'\\'); wchar_t* sf = wcsrchr(dir, L'/');
  if (sf && (!sl || sf > sl)) sl = sf;  // accept both separators (device uses '\\', a server test may pass '/')
  if (sl) *sl = 0;

  int before = (strcmp(when, "before") == 0);
  int after = (strcmp(when, "after") == 0);

  if (after) { BOOL r = RevertToSelf(); dbg("DP|revert(before load)|%s err=%lu", r ? "OK" : "FAIL", r ? 0UL : GetLastError()); }

  // read-backs under the effective token at load time (C)
  const char* open_r = rb_open(dllpath);
  DWORD attr = GetFileAttributesW(dir);
  const char* attr_r = attr != INVALID_FILE_ATTRIBUTES ? "OK" : (GetLastError() == ERROR_ACCESS_DENIED ? "ACCESS_DENIED" : "OTHER");
  dbg("DP|readback|open_read_exec=%s|getfileattributes_appdir=%s", open_r, attr_r);

  if (snaps) dbg("DP|snaps|%s", set_ldr_snaps() ? "FLG_SHOW_LDR_SNAPS set" : "could not set PEB flag");

  SetCurrentDirectoryW(dir);  // as MainDllLoader does
  HMODULE h = LoadLibraryExW(dllpath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  DWORD err = h ? 0 : GetLastError();
  dbg("DP|load|handle=%p err=%lu (%s)", (void*)h, err, errname(err));

  if (before) { BOOL r = RevertToSelf(); dbg("DP|revert(after load)|%s err=%lu", r ? "OK" : "FAIL", r ? 0UL : GetLastError()); }

  dbg("DP|done|err=%lu", err);
  return (int)err;
}

// ---------------------------------------------------------------------------------------------------------------------
// Alternate desktop (as the sandbox broker and sbxprobe give their restricted children) - a low-integrity / lockdown token
// cannot initialize on the interactive desktop, so without this the restricted child faults in user/CSRSS init. user32 is loaded
// dynamically (kept out of the static import table, like sbxprobe), so the 15035 imports check stays kernel32/advapi32 only.
typedef HDESK(WINAPI* CreateDesktopW_t)(LPCWSTR, LPCWSTR, void*, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES);
typedef HWINSTA(WINAPI* GetProcessWindowStation_t)(void);
typedef BOOL(WINAPI* GetUserObjectInformationW_t)(HANDLE, int, PVOID, DWORD, LPDWORD);
static wchar_t g_desk[160];
static void make_alt_desktop(void) {
  HMODULE u = LoadLibraryW(L"user32.dll");
  if (!u) return;
  CreateDesktopW_t cd = (CreateDesktopW_t)(void*)GetProcAddress(u, "CreateDesktopW");
  GetProcessWindowStation_t gws = (GetProcessWindowStation_t)(void*)GetProcAddress(u, "GetProcessWindowStation");
  GetUserObjectInformationW_t gui = (GetUserObjectInformationW_t)(void*)GetProcAddress(u, "GetUserObjectInformationW");
  if (!cd || !gws || !gui) return;
  HDESK d = cd(L"dllprobe_desktop", NULL, NULL, 0, GENERIC_ALL, NULL);
  if (!d) { say("  (CreateDesktopW error %lu; restricted rows use the inherited desktop)\n", GetLastError()); return; }
  wchar_t wn[128] = {0}; DWORD need = 0;
  if (gui(gws(), 2 /*UOI_NAME*/, wn, sizeof wn, &need) && wn[0]) _snwprintf(g_desk, 160, L"%s\\dllprobe_desktop", wn);
  else wcscpy(g_desk, L"dllprobe_desktop");
}

// ---------------------------------------------------------------------------------------------------------------------
// Parent: create the child with the given PRIMARY token, suspended + debugged; for the sandbox shapes set the broker's initial
// impersonation token on the main thread (so the child can RevertToSelf to the restricted primary); resume; read its loader
// snaps and DP| messages from the debug stream; report the row.
static void enable_debug_priv(void) {
  HANDLE t; TOKEN_PRIVILEGES tp; LUID luid;
  if (pOpenProcessToken && pOpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &t)) {
    if (LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &luid)) {
      tp.PrivilegeCount = 1; tp.Privileges[0].Luid = luid; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      AdjustTokenPrivileges(t, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(t);
  }
}

static void run_row(const char* label, int kind, const char* when, int snaps, const wchar_t* selfpath, const wchar_t* dllpath) {
  say("\n[%s]  primary-token kind %d, load %s, snaps %d\n", label, kind, when, snaps);
  HANDLE primary = make_token(kind);
  if (!primary) { say("  could not build the primary token (kind %d): error %lu  -- STRUCTURAL note, builder unavailable\n", kind, GetLastError()); return; }
  HANDLE initial = (kind == 5 || kind == 6) ? make_initial_impersonation() : NULL;

  wchar_t cmd[2048];
  _snwprintf(cmd, 2048, L"\"%s\" --load %d %S %d \"%s\"", selfpath, kind, when, snaps, dllpath);

  STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  if ((kind == 5 || kind == 6) && g_desk[0]) si.lpDesktop = g_desk;  // restricted tokens need the alternate desktop to init
  PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
  int nodebug = (GetEnvironmentVariableW(L"DLLPROBE_NODEBUG", NULL, 0) != 0);
  DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | (nodebug ? 0 : DEBUG_ONLY_THIS_PROCESS);
  BOOL ok = pCreateProcessAsUserW ? pCreateProcessAsUserW(primary, selfpath, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi) : FALSE;
  if (!ok) { say("  CreateProcessAsUserW failed: error %lu (%s)  -- the restricted token could not even start the child\n", GetLastError(), errname(GetLastError())); CloseHandle(primary); if (initial) CloseHandle(initial); return; }

  int doimp = (GetEnvironmentVariableW(L"DLLPROBE_IMP", NULL, 0) != 0);  // opt-in: the real broker's initial token is more
  if (initial && pSetThreadToken && doimp) {                             // nuanced than our own-token dup, which destabilizes init
    if (!pSetThreadToken(&pi.hThread, initial)) say("  (SetThreadToken(initial) failed: error %lu)\n", GetLastError());
    else say("  initial broker token impersonated on the child main thread\n");
  }
  ResumeThread(pi.hThread);

  int snaps_shown = 0; DWORD child_exit = 0xFFFFFFFF;
  if (nodebug) {
    WaitForSingleObject(pi.hProcess, 30000);
    GetExitCodeProcess(pi.hProcess, &child_exit);
    say("  child exit (load GetLastError) = %lu (%s)  [no-debug diagnostic: snaps not captured]\n", child_exit, errname(child_exit));
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(primary); if (initial) CloseHandle(initial);
    return;
  }
  // debug loop: capture OUTPUT_DEBUG_STRING (loader snaps + the child's DP| lines) until the child exits
  for (;;) {
    DEBUG_EVENT de; ZeroMemory(&de, sizeof de);
    if (!WaitForDebugEvent(&de, 30000)) { say("  (WaitForDebugEvent timed out/failed: error %lu)\n", GetLastError()); break; }
    DWORD cont = DBG_CONTINUE;
    if (de.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT) {
      OUTPUT_DEBUG_STRING_INFO* o = &de.u.DebugString;
      SIZE_T n = o->nDebugStringLength; if (n > 2048) n = 2048;
      char* raw = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n + 2);
      wchar_t* wraw = (wchar_t*)raw;
      SIZE_T got = 0;
      if (raw && ReadProcessMemory(pi.hProcess, o->lpDebugStringData, raw, n, &got)) {
        char line[2048];
        if (o->fUnicode) { wraw[(got / 2 < 1023) ? got / 2 : 1023] = 0; _snprintf(line, sizeof line, "%S", wraw); }
        else { raw[(got < 2047) ? got : 2047] = 0; _snprintf(line, sizeof line, "%s", raw); }
        // strip trailing newline
        size_t L = strlen(line); while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (strncmp(line, "DP|", 3) == 0) {
          say("    %s\n", line);
        } else if (snaps) {
          // loader snaps - keep the ones that name a module or a failure (and cap the volume)
          if (snaps_shown < 60 && (strstr(line, "LDR:") || strstr(line, ".dll") || strstr(line, "not found") ||
                                   strstr(line, "could not") || strstr(line, "ERROR") || strstr(line, "Unable"))) {
            say("    snap: %s\n", line); snaps_shown++;
          }
        }
      }
      if (raw) HeapFree(GetProcessHeap(), 0, raw);
    } else if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
      // let the first breakpoint through quietly; pass real exceptions back to the child
      DWORD code = de.u.Exception.ExceptionRecord.ExceptionCode;
      if (code == EXCEPTION_BREAKPOINT || code == 0x4000001F /*STATUS_WX86_BREAKPOINT*/) cont = DBG_CONTINUE;
      else { cont = DBG_EXCEPTION_NOT_HANDLED; say("    (child exception 0x%08lX)\n", code); }
    } else if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      child_exit = de.u.ExitProcess.dwExitCode;
      ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
      break;
    }
    ContinueDebugEvent(de.dwProcessId, de.dwThreadId, cont);
  }
  say("  child exit (load GetLastError) = %lu (%s)\n", child_exit, errname(child_exit));
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  CloseHandle(primary); if (initial) CloseHandle(initial);
}

// ---------------------------------------------------------------------------------------------------------------------
// Robust, crash-free measurement: in the already-initialized parent, IMPERSONATE the restricted token on this thread and do the
// read-backs and per-dependency accessibility checks. File access (CreateFileW, loader opens) honors thread impersonation, so this
// faithfully answers the file-access hypotheses the batch leads with - H1 (can the token READ chrome.dll / the app folder) and H2
// (which imported DLL is not findable/readable) - without a restricted-primary-token child that cannot even initialize here.
// A separate faithful child-primary-token load (run_row) is still attempted; on this server its restricted child fails in init
// (noted), but on the device it reproduces the renderer.

// Parse chrome.dll's imported + delay-imported DLL names (done as the parent's own token; names only, no access check here).
// The probe's arch matches the chrome.dll under test (arm32<->arm32, x64<->x64), so IMAGE_NT_HEADERS is the right width.
static DWORD rva2off(IMAGE_SECTION_HEADER* sec, int nsec, DWORD rva) {
  for (int s = 0; s < nsec; ++s)
    if (rva >= sec[s].VirtualAddress && rva < sec[s].VirtualAddress + sec[s].Misc.VirtualSize)
      return rva - sec[s].VirtualAddress + sec[s].PointerToRawData;
  return 0;
}
static int pe_import_dlls(const wchar_t* path, char names[][64], int maxn) {
  int count = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) return -1;
  HANDLE m = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
  BYTE* base = m ? (BYTE*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : NULL;
  if (base) {
    __try {
      IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
      if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {
          IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
          int nsec = nt->FileHeader.NumberOfSections;
          DWORD irva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
          if (irva) {
            IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + rva2off(sec, nsec, irva));
            for (; imp->Name && count < maxn; ++imp) {
              _snprintf(names[count], 64, "%s", (const char*)(base + rva2off(sec, nsec, imp->Name))); names[count][63] = 0; count++;
            }
          }
          DWORD drva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
          if (drva) {
            IMAGE_DELAYLOAD_DESCRIPTOR* dly = (IMAGE_DELAYLOAD_DESCRIPTOR*)(base + rva2off(sec, nsec, drva));
            for (; dly->DllNameRVA && count < maxn; ++dly) {
              _snprintf(names[count], 64, "%s(delay)", (const char*)(base + rva2off(sec, nsec, dly->DllNameRVA))); names[count][63] = 0; count++;
            }
          }
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { count = -2; }
    UnmapViewOfFile(base);
  }
  if (m) CloseHandle(m);
  CloseHandle(f);
  return count;
}

// under the currently-effective (impersonated) token: can this dependency be found (in chrome.dll's own folder first, as
// LOAD_WITH_ALTERED_SEARCH_PATH does, then the standard search = System32 etc.) and read?
static const char* dep_access(const wchar_t* appdir, const char* dllname) {
  char clean[64]; _snprintf(clean, 64, "%s", dllname);
  char* p = strstr(clean, "(delay)"); if (p) *p = 0;
  wchar_t w[64]; MultiByteToWideChar(CP_ACP, 0, clean, -1, w, 64);
  wchar_t full[MAX_PATH]; full[0] = 0;
  // 1) the app folder (where LOAD_WITH_ALTERED_SEARCH_PATH looks first)
  _snwprintf(full, MAX_PATH, L"%s\\%s", appdir, w);
  if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) {
    // 2) the standard DLL search path (System32 etc.)
    if (!SearchPathW(NULL, w, NULL, MAX_PATH, full, NULL)) return "NOT_FOUND";
  }
  HANDLE h = CreateFileW(full, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_ACCESS_DENIED ? "found_but_ACCESS_DENIED" : "found_but_OPEN_FAILED";
  CloseHandle(h);
  return "OK";
}

// impersonate the restricted token (kind 5/6) or run as self (kind 1 control), then measure. Returns to self.
static void measure_impersonated(const char* label, int kind, const wchar_t* dllpath, char names[][64], int ndeps) {
  say("\n[%s]  (parent impersonation measurement - robust, works on server and device)\n", label);
  HANDLE imp = NULL;
  if (kind == 5 || kind == 6) {
    HANDLE own = NULL;
    if (pOpenProcessToken && pOpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &own)) {
      HANDLE restricted = (kind == 5) ? make_lockdown_token(own) : make_limited_token(own);
      if (restricted) { if (!pDuplicateTokenEx(restricted, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &imp)) imp = NULL; CloseHandle(restricted); }
      CloseHandle(own);
    }
    if (!imp) { say("  could not build the %s impersonation token: error %lu\n", kind == 5 ? "lockdown" : "limited", GetLastError()); return; }
    if (!pSetThreadToken || !pSetThreadToken(NULL, imp)) { say("  SetThreadToken(self) failed: error %lu\n", GetLastError()); CloseHandle(imp); return; }
    say("  impersonating the %s token on this thread\n", kind == 5 ? "USER_LOCKDOWN-like" : "USER_LIMITED-like");
  } else {
    say("  (own token - the positive control)\n");
  }

  // read-backs (H1)
  wchar_t dir[MAX_PATH]; wcsncpy(dir, dllpath, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
  { wchar_t* a = wcsrchr(dir, L'\\'); wchar_t* b = wcsrchr(dir, L'/'); if (b && (!a || b > a)) a = b; if (a) *a = 0; }
  const char* open_r = rb_open(dllpath);
  DWORD attr = GetFileAttributesW(dir);
  const char* attr_r = attr != INVALID_FILE_ATTRIBUTES ? "OK" : (GetLastError() == ERROR_ACCESS_DENIED ? "ACCESS_DENIED" : "OTHER");
  say("  H1 read-backs: open(chrome.dll, READ|EXECUTE)=%s ; GetFileAttributes(app folder)=%s\n", open_r, attr_r);

  // per-dependency accessibility (H2) - name any dependency not findable/readable under this token
  if (ndeps > 0) {
    int bad = 0;
    for (int i = 0; i < ndeps; ++i) {
      const char* r = dep_access(dir, names[i]);
      if (strcmp(r, "OK") != 0) { say("  H2 dependency NOT OK: %s -> %s\n", names[i], r); bad++; }
    }
    say("  H2 dependencies: %d imported module(s) checked, %d not OK under this token\n", ndeps, bad);
  }

  // best-effort load under impersonation (A)
  SetCurrentDirectoryW(dir);
  HMODULE h = LoadLibraryExW(dllpath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  DWORD err = h ? 0 : GetLastError();
  say("  A load under this token: handle=%p err=%lu (%s)\n", (void*)h, err, errname(err));
  if (h) FreeLibrary(h);

  if (imp) { RevertToSelf(); CloseHandle(imp); }
}

// ---------------------------------------------------------------------------------------------------------------------
static void arch_name(char* out, size_t n) {
#if defined(_M_ARM)
  _snprintf(out, n, "ARM32 (ARMNT)");
#elif defined(_M_X64)
  _snprintf(out, n, "x64");
#elif defined(_M_IX86)
  _snprintf(out, n, "x86");
#else
  _snprintf(out, n, "?");
#endif
}

int wmain(int argc, wchar_t** argv) {
  // child mode first - the child does not need the broker token builders (load_advapi), only kernel32/advapi32 statics
  if (argc >= 6 && wcscmp(argv[1], L"--load") == 0) {
    if (GetEnvironmentVariableW(L"DLLPROBE_EARLYEXIT", NULL, 0) != 0) ExitProcess(77);  // diagnostic: did the child reach wmain?
    int kind = _wtoi(argv[2]);
    char when[16]; _snprintf(when, sizeof when, "%S", argv[3]);
    int snaps = _wtoi(argv[4]);
    return do_load_child(kind, when, snaps, argv[5]);
  }

  load_advapi();

  // parent: resolve chrome.dll path (--dll or next to the exe), open the report file
  wchar_t selfpath[MAX_PATH]; GetModuleFileNameW(NULL, selfpath, MAX_PATH);
  wchar_t dllpath[MAX_PATH] = {0};
  for (int i = 1; i + 1 < argc; ++i) if (wcscmp(argv[i], L"--dll") == 0) { wcsncpy(dllpath, argv[i+1], MAX_PATH-1); }
  if (!dllpath[0]) { wcsncpy(dllpath, selfpath, MAX_PATH-1); wchar_t* s = wcsrchr(dllpath, L'\\'); if (s) wcscpy(s+1, L"chrome.dll"); }

  SYSTEMTIME st; GetLocalTime(&st);
  wchar_t logname[MAX_PATH];
  { wchar_t dir[MAX_PATH]; wcsncpy(dir, selfpath, MAX_PATH-1); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0;
    _snwprintf(logname, MAX_PATH, L"%s\\dllprobe-%04d%02d%02d-%02d%02d%02d.txt", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); }
  g_log = _wfopen(logname, L"w");  // text mode (\n -> \r\n for the device's Notepad); content is ASCII

  enable_debug_priv();
  make_alt_desktop();
  char arch[32]; arch_name(arch, sizeof arch);
  say("dllprobe (BATCH-SBX-1) - %s - %04d-%02d-%02d %02d:%02d:%02d\n", arch, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  say("Reproduces chrome\\app\\main_dll_loader_win.cc's load (SetCurrentDirectoryW + LoadLibraryExW(LOAD_WITH_ALTERED_SEARCH_PATH))\n");
  { DWORD a = GetFileAttributesW(dllpath); say("chrome.dll under test: %S  (%s)\n", dllpath, a != INVALID_FILE_ATTRIBUTES ? "present" : "NOT PRESENT - rows will show the load error"); }
  say("Legend: H1 label/ACL (read-backs ACCESS_DENIED), H2 dependency (snaps name X.dll), H3/H4 order/token (before loads, after fails).\n");
  if (!pCreateProcessAsUserW || !pCreateRestrictedToken) say("WARNING: token APIs unavailable; rows will be limited.\n");

  // chrome.dll's imported DLL names (parsed as the parent's own token), used by the per-dependency accessibility check
  static char names[96][64];
  int nd = pe_import_dlls(dllpath, names, 96);
  if (nd < 0) say("\n(could not parse chrome.dll's import table: code %d - the dependency check is skipped)\n", nd);
  else { say("\nchrome.dll imports %d module(s):", nd); for (int i = 0; i < nd; ++i) say(" %s", names[i]); say("\n"); }

  say("\n=== Part A: robust impersonation measurement (the reliable answer - runs the same on server and device) ===\n");
  measure_impersonated("control: own token", 1, dllpath, names, nd < 0 ? 0 : nd);
  measure_impersonated("GPU shape: USER_LIMITED-like", 6, dllpath, names, nd < 0 ? 0 : nd);
  measure_impersonated("RENDERER shape: USER_LOCKDOWN-like", 5, dllpath, names, nd < 0 ? 0 : nd);

  say("\n=== Part B: faithful child under the restricted PRIMARY token (as the broker makes it; snaps when it runs) ===\n");
  say("(On THIS server the restricted-primary child fails in init - its ACCESS_DENIED/0xC0000005 is a server artifact, not the device answer. On the device it reproduces the renderer. Set DLLPROBE_NODEBUG=1 to skip the debugger, DLLPROBE_IMP=1 to add an initial-token impersonation.)\n");
  run_row("control: own token, plain load", 1, "plain", 1, selfpath, dllpath);
  run_row("GPU shape: USER_LIMITED, load after RevertToSelf", 6, "after", 1, selfpath, dllpath);
  run_row("RENDERER shape: USER_LOCKDOWN, load AFTER token drop (the device case)", 5, "after", 1, selfpath, dllpath);
  run_row("RENDERER shape: USER_LOCKDOWN, load BEFORE token drop (H3/H4)", 5, "before", 1, selfpath, dllpath);

  say("\nReading the result (use Part A; Part B confirms on the device):\n");
  say("  - RENDERER H1 read-back open(chrome.dll, READ|EXECUTE)=ACCESS_DENIED          -> H1 (label/ACL): the lockdown token cannot read chrome.dll; grant the exe tree read+execute.\n");
  say("  - RENDERER open=OK, but a dependency shows NOT_FOUND / found_but_ACCESS_DENIED  -> H2 (dependency search): that named module is the missing one.\n");
  say("  - RENDERER open=OK, all dependencies OK, yet Part B's child load fails after the token drop but the control/GPU load OK -> H3/H4 (order/token): load chrome.dll before LowerToken.\n");
  say("  NOTE (server): the server's own token/job may make restricted read-backs or the Part B child fail for reasons unrelated to the device. The Surface 2 run is the answer.\n");
  say("\nWrote %S\n", logname);
  if (g_log) fclose(g_log);
  return 0;
}
