// gpuprobe.c - Venetium GPU probe v2 (2026-11-10): what a Windows ARM32 device's GPU and its driver really offer.
//
// First target: a Samsung Galaxy Note Pro 12.2 (SM-P900, Exynos 5420, Mali-T628) running Windows RT 8.1, where Venetium runs
// without hardware acceleration. It answers the questions Venetium's GPU choices depend on: is there a real GPU driver or
// only Microsoft's basic display / render driver, which vendor/device ids and driver version does Chromium see, the highest
// Direct3D 11 feature level (Venetium 0045 picks ANGLE D3D11 at >= 9_3, else D3D9), the D3D9 caps, whether either API
// actually DRAWS correctly (pixel readback), swapchains, DirectComposition, and hardware video decode (D3D11 video, DXVA2,
// Media Foundation). It only reads and draws off-screen; it changes nothing on the machine.
//
// v1 (d3dprobe) faulted in its D3D11 video section on every device: it FreeLibrary'd d3d11.dll (loaded only at run time)
// while the D3D11 device was still alive, so the next call jumped into unloaded code. v2 keeps every DLL it loads, and runs
// each section in its own child process with a timeout, so a driver crash or hang costs one section, reported by exit code.
//
// Usage:  gpuprobe.exe [output-dir]          full run; writes gpuprobe-<date>-<time>.txt + gpuprobe-report.txt
//         gpuprobe.exe --section <name>      one section to stdout (used by the full run)
//         gpuprobe.exe --save-clipboard <f>  saves the clipboard text to <f> (chrome://gpu "Copy Report to Clipboard")
//         gpuprobe.exe --clear-clipboard     empties the clipboard (between render-check runs)
// C with COBJMACROS; every graphics/media DLL is loaded at run time, so the exe imports only kernel32/user32/gdi32/advapi32/ole32.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <stdarg.h>
#include <io.h>
#include <fcntl.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_2.h>
#include <dxgi1_2.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
// dcomp.h has no C bindings: only the IID is needed (the device is released as an IUnknown)
DEFINE_GUID(IID_IDCompositionDevice, 0xC37EA93A, 0xE7AA, 0x450D, 0xB1, 0x6F, 0x97, 0x46, 0xCB, 0x04, 0x07, 0xF3);

#define PROBE_VERSION "gpuprobe 2.0 (Venetium, 2026-11-10)"
// Negative control (build with /DPROBE_NEGATIVE_CONTROL, never shipped): the draw tests output a wrong colour (must read
// WRONG), d3d11video fast-fails (the runner must report CRASHED 0xC0000409 and go on), mf hangs (must report HUNG).
#ifdef PROBE_NEGATIVE_CONTROL
#define DRAW_COLOR "1,0,0,1"
#else
#define DRAW_COLOR "0.25,0.5,0.75,1"
#endif

// ------------------------------------------------------------------------------------------------ output (child: stdout)
static void P(const char* fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
  fwrite(buf, 1, (size_t)n, stdout);
  fflush(stdout);
}
static void W2U(const wchar_t* w, char* out, int cap) {
  if (!w) { out[0] = 0; return; }
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
  if (n <= 0) out[0] = 0;
}
#define S(label) P("\n== %s ==\n", label)
// a step that may fault inside a driver: report the exception and go on with the next step
#define TRYSTEP(name, stmt)                                                                         \
  __try { stmt; } __except (EXCEPTION_EXECUTE_HANDLER) {                                           \
    P("  [%s faulted: exception 0x%08lX]\n", name, (unsigned long)GetExceptionCode());              \
  }

static const char* fl_name(D3D_FEATURE_LEVEL f) {
  switch (f) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    case D3D_FEATURE_LEVEL_9_3: return "9_3";
    case D3D_FEATURE_LEVEL_9_2: return "9_2";
    case D3D_FEATURE_LEVEL_9_1: return "9_1";
    default: return "?";
  }
}

// ------------------------------------------------------------------------------------------------ known decoder GUIDs
// (the same values serve DXVA2 decoder GUIDs and D3D11 decoder profiles)
typedef struct { GUID g; const char* name; } NamedGuid;
static const NamedGuid kDecoders[] = {
  {{0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "H.264 VLD (no FGT)"},
  {{0x1b81be69, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "H.264 VLD FGT"},
  {{0xd5f04ff9, 0x3418, 0x45d8, {0x95, 0x61, 0x32, 0xa7, 0x6a, 0xae, 0x2d, 0xdd}}, "H.264 VLD with FMO/ASO (no FGT)"},
  {{0x1b81be64, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "H.264 MoComp (no FGT)"},
  {{0x1b81be66, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "H.264 IDCT (no FGT)"},
  {{0xee27417f, 0x5e28, 0x4e65, {0xbe, 0xea, 0x1d, 0x26, 0xb5, 0x08, 0xad, 0xc9}}, "MPEG-2 VLD"},
  {{0x86695f12, 0x340e, 0x4f04, {0x9f, 0xd3, 0x92, 0x53, 0xdd, 0x32, 0x74, 0x60}}, "MPEG-2 and MPEG-1 VLD"},
  {{0x1b81bea3, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "VC-1 VLD"},
  {{0x1b81bea4, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "VC-1 VLD 2010"},
  {{0x1b81be94, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}}, "WMV9 IDCT"},
  {{0x5b11d51b, 0x2f4c, 0x4452, {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}}, "HEVC Main"},
  {{0x107af0e0, 0xef1a, 0x4d19, {0xab, 0xa8, 0x67, 0xa1, 0x63, 0x07, 0x3d, 0x13}}, "HEVC Main10"},
  {{0x463707f8, 0xa1d0, 0x4585, {0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e}}, "VP9 Profile 0"},
  {{0xa4c749ef, 0x6ecf, 0x48aa, {0x84, 0x48, 0x50, 0xa7, 0xa1, 0x16, 0x5f, 0xf7}}, "VP9 10-bit Profile 2"},
  {{0x90b899ea, 0x3a62, 0x4705, {0x88, 0xb3, 0x8d, 0xf0, 0x4b, 0x27, 0x44, 0xe7}}, "VP8"},
  {{0xb8be4ccb, 0xcf53, 0x46ba, {0x8d, 0x59, 0xd6, 0xb8, 0xa6, 0xda, 0x5d, 0x2a}}, "AV1 Profile 0"},
  {{0xefd64d74, 0xc9e8, 0x41d7, {0xa5, 0xe9, 0xe9, 0xb0, 0xe3, 0x9f, 0xa3, 0x19}}, "MPEG-4 Part 2 VLD Simple"},
  {{0xed418a9f, 0x010d, 0x4eda, {0x9a, 0xe3, 0x9a, 0x65, 0x35, 0x8d, 0x8d, 0x2e}}, "MPEG-4 Part 2 VLD Advanced Simple"},
};
static const GUID kH264 = {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
static const char* guid_name(const GUID* g) {
  for (size_t i = 0; i < sizeof kDecoders / sizeof kDecoders[0]; ++i)
    if (IsEqualGUID(g, &kDecoders[i].g)) return kDecoders[i].name;
  return "unknown";
}
static void guid_str(const GUID* g, char* out, size_t cap) {
  snprintf(out, cap, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2, g->Data3,
           g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

// ------------------------------------------------------------------------------------------------ registry helpers
static void reg_print_value(HKEY k, const wchar_t* name, const char* indent) {
  BYTE data[8192];
  DWORD type = 0, size = sizeof data - 4;
  if (RegQueryValueExW(k, name, NULL, &type, data, &size) != ERROR_SUCCESS) return;
  char n8[256], v8[8192];
  W2U(name, n8, sizeof n8);
  data[size] = 0; data[size + 1] = 0; data[size + 2] = 0; data[size + 3] = 0;
  if (type == REG_SZ || type == REG_EXPAND_SZ) {
    W2U((const wchar_t*)data, v8, sizeof v8);
    P("%s%s = %s\n", indent, n8, v8);
  } else if (type == REG_MULTI_SZ) {
    P("%s%s =", indent, n8);
    for (const wchar_t* s = (const wchar_t*)data; *s; s += wcslen(s) + 1) { W2U(s, v8, sizeof v8); P(" [%s]", v8); }
    P("\n");
  } else if (type == REG_DWORD && size >= 4) {
    P("%s%s = 0x%08lX (%lu)\n", indent, n8, *(DWORD*)data, *(DWORD*)data);
  } else if (type == REG_QWORD && size >= 8) {
    P("%s%s = 0x%016llX\n", indent, n8, *(unsigned long long*)data);
  } else if (type == REG_BINARY) {
    // HardwareInformation.* strings are UTF-16 in REG_BINARY; MemorySize is a DWORD/QWORD
    int printable = size >= 4 && (size % 2) == 0;
    for (DWORD i = 0; printable && i + 1 < size; i += 2) {
      wchar_t c = *(wchar_t*)(data + i);
      if (c == 0 && i + 2 >= size) break;
      if (c < 0x20 && c != 0) printable = 0;
    }
    if (printable && size > 8) { W2U((const wchar_t*)data, v8, sizeof v8); P("%s%s = \"%s\" (binary text)\n", indent, n8, v8); }
    else if (size == 4) P("%s%s = %lu (binary dword)\n", indent, n8, *(DWORD*)data);
    else if (size == 8) P("%s%s = %llu (binary qword)\n", indent, n8, *(unsigned long long*)data);
    else P("%s%s = <binary %lu bytes>\n", indent, n8, size);
  }
}
static void reg_print_key(HKEY root, const wchar_t* path, const wchar_t* const* names, const char* indent) {
  HKEY k;
  if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) {
    char p8[512]; W2U(path, p8, sizeof p8); P("%s(no key %s)\n", indent, p8); return;
  }
  for (int i = 0; names[i]; ++i) reg_print_value(k, names[i], indent);
  RegCloseKey(k);
}

// ================================================================================================ sections
static void section_system(void) {
  S("system");
  typedef LONG(WINAPI * RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW v;
  ZeroMemory(&v, sizeof v);
  v.dwOSVersionInfoSize = sizeof v;
  RtlGetVersion_t rgv = (RtlGetVersion_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  if (rgv) rgv(&v);
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  P("  windows %lu.%lu.%lu, native arch %s, %lu logical processors\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
    si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM ? "ARM32"
    : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64"
    : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86"
    : si.wProcessorArchitecture == 12 ? "ARM64" : "?", si.dwNumberOfProcessors);
  static const wchar_t* const os[] = {L"ProductName", L"EditionID", L"CurrentBuild", L"UBR", L"BuildLabEx", NULL};
  reg_print_key(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", os, "  ");
  static const wchar_t* const bios[] = {L"SystemManufacturer", L"SystemProductName", L"SystemFamily", L"SystemSKU",
                                        L"SystemVersion", L"BaseBoardManufacturer", L"BaseBoardProduct", L"BIOSVendor",
                                        L"BIOSVersion", L"BIOSReleaseDate", NULL};
  reg_print_key(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", bios, "  ");
  static const wchar_t* const cpu[] = {L"ProcessorNameString", L"Identifier", L"VendorIdentifier", L"~MHz", NULL};
  reg_print_key(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", cpu, "  cpu0: ");
  P("  ARM features: NEON %d, VFP-D32 %d, hardware divide %d, 64-bit atomics %d, FMAC %d\n",
    IsProcessorFeaturePresent(19), IsProcessorFeaturePresent(18), IsProcessorFeaturePresent(24),
    IsProcessorFeaturePresent(25), IsProcessorFeaturePresent(27));
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof ms;
  if (GlobalMemoryStatusEx(&ms))
    P("  memory: %llu MB total, %llu MB free, %lu%% in use\n", ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20, ms.dwMemoryLoad);
  P("SUMMARY: windows %lu.%lu.%lu, %lu cpus\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, si.dwNumberOfProcessors);
}

static void section_display(void) {
  S("display adapters, monitors and drivers");
  DISPLAY_DEVICEW dd;
  char a[512], b[512];
  for (DWORD i = 0;; ++i) {
    ZeroMemory(&dd, sizeof dd);
    dd.cb = sizeof dd;
    if (!EnumDisplayDevicesW(NULL, i, &dd, 0)) break;
    W2U(dd.DeviceString, a, sizeof a);
    W2U(dd.DeviceID, b, sizeof b);
    P("  display device %lu: \"%s\" id %s flags 0x%lX%s%s\n", i, a, b, dd.StateFlags,
      (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ? " attached" : "",
      (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " PRIMARY" : "");
    if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
      DEVMODEW dm;
      ZeroMemory(&dm, sizeof dm);
      dm.dmSize = sizeof dm;
      if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
        P("    mode %lux%lu, %lu bpp, %lu Hz, orientation %lu\n", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmBitsPerPel,
          dm.dmDisplayFrequency, dm.dmDisplayOrientation);
      DISPLAY_DEVICEW mon;
      for (DWORD j = 0;; ++j) {
        ZeroMemory(&mon, sizeof mon);
        mon.cb = sizeof mon;
        if (!EnumDisplayDevicesW(dd.DeviceName, j, &mon, 0)) break;
        W2U(mon.DeviceString, a, sizeof a);
        W2U(mon.DeviceID, b, sizeof b);
        P("    monitor %lu: \"%s\" id %s\n", j, a, b);
      }
    }
  }
  HDC dc = GetDC(NULL);
  if (dc) {
    P("  desktop DPI %d x %d\n", GetDeviceCaps(dc, LOGPIXELSX), GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(NULL, dc);
  }
  // the installed display drivers (class GUID of display adapters)
  const wchar_t* cls = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
  HKEY ck;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, cls, 0, KEY_READ, &ck) == ERROR_SUCCESS) {
    wchar_t sub[64];
    for (DWORD i = 0;; ++i) {
      DWORD n = 64;
      if (RegEnumKeyExW(ck, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      if (sub[0] < L'0' || sub[0] > L'9') continue;
      wchar_t path[256];
      swprintf(path, 256, L"%ls\\%ls", cls, sub);
      W2U(sub, a, sizeof a);
      P("  driver %s:\n", a);
      static const wchar_t* const vals[] = {
        L"DriverDesc", L"ProviderName", L"DriverVersion", L"DriverDate", L"MatchingDeviceId", L"InfPath", L"InfSection",
        L"UserModeDriverName", L"UserModeDriverNameWow", L"InstalledDisplayDrivers", L"FeatureScore",
        L"HardwareInformation.AdapterString", L"HardwareInformation.ChipType", L"HardwareInformation.DacType",
        L"HardwareInformation.MemorySize", L"HardwareInformation.qwMemorySize", L"HardwareInformation.BiosString", NULL};
      reg_print_key(HKEY_LOCAL_MACHINE, path, vals, "    ");
    }
    RegCloseKey(ck);
  } else {
    P("  (display driver class key not readable)\n");
  }
  P("  OpenGL ICDs registered:\n");
  HKEY gk;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers", 0, KEY_READ,
                    &gk) == ERROR_SUCCESS) {
    wchar_t sub[128];
    for (DWORD i = 0;; ++i) {
      DWORD n = 128;
      if (RegEnumKeyExW(gk, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      W2U(sub, a, sizeof a);
      P("    %s\n", a);
    }
    RegCloseKey(gk);
  } else {
    P("    (none)\n");
  }
}

// ------------------------------------------------------------------------------------------------ DXGI
typedef HRESULT(WINAPI* CreateDXGIFactory1_t)(REFIID, void**);
static CreateDXGIFactory1_t load_dxgi(void) {
  HMODULE m = LoadLibraryW(L"dxgi.dll");  // kept loaded for the life of the process
  return m ? (CreateDXGIFactory1_t)(void*)GetProcAddress(m, "CreateDXGIFactory1") : NULL;
}

static void section_dxgi(void) {
  S("DXGI adapters (what Chromium reads as GPU vendor / device / driver version)");
  CreateDXGIFactory1_t pCreate = load_dxgi();
  if (!pCreate) { P("  CreateDXGIFactory1 not available\n"); return; }
  IDXGIFactory1* f = NULL;
  HRESULT hr = pCreate(&IID_IDXGIFactory1, (void**)&f);
  if (FAILED(hr) || !f) { P("  CreateDXGIFactory1 failed 0x%08lX\n", (unsigned long)hr); return; }
  IDXGIAdapter1* a1 = NULL;
  char d8[256];
  for (UINT i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a1) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 d;
    if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a1, &d))) {
      W2U(d.Description, d8, sizeof d8);
      int basic = d.VendorId == 0x1414 && (d.DeviceId == 0x8c || d.DeviceId == 0x8d);
      P("  adapter %u: \"%s\"\n    vendor 0x%04X device 0x%04X subsys 0x%08X rev %u flags 0x%X%s%s\n"
        "    dedicated video %llu MB, dedicated system %llu MB, shared system %llu MB\n",
        i, d8, d.VendorId, d.DeviceId, d.SubSysId, d.Revision, (unsigned)d.Flags,
        (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " SOFTWARE" : "",
        basic ? " (Microsoft Basic Render Driver = software, no GPU driver)" : "",
        (unsigned long long)(d.DedicatedVideoMemory >> 20), (unsigned long long)(d.DedicatedSystemMemory >> 20),
        (unsigned long long)(d.SharedSystemMemory >> 20));
      LARGE_INTEGER umd;
      HRESULT ch = IDXGIAdapter1_CheckInterfaceSupport(a1, &IID_IDXGIDevice, &umd);
      if (SUCCEEDED(ch))
        P("    user-mode driver version %u.%u.%u.%u\n", HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart),
          LOWORD(umd.LowPart));
      else
        P("    user-mode driver version: CheckInterfaceSupport 0x%08lX\n", (unsigned long)ch);
      IDXGIAdapter2* a2 = NULL;
      if (SUCCEEDED(IDXGIAdapter1_QueryInterface(a1, &IID_IDXGIAdapter2, (void**)&a2)) && a2) {
        DXGI_ADAPTER_DESC2 d2;
        if (SUCCEEDED(IDXGIAdapter2_GetDesc2(a2, &d2)))
          P("    preemption: graphics %d, compute %d\n", d2.GraphicsPreemptionGranularity, d2.ComputePreemptionGranularity);
        IDXGIAdapter2_Release(a2);
      }
      IDXGIOutput* o = NULL;
      for (UINT j = 0; IDXGIAdapter1_EnumOutputs(a1, j, &o) == S_OK; ++j) {
        DXGI_OUTPUT_DESC od;
        if (SUCCEEDED(IDXGIOutput_GetDesc(o, &od))) {
          W2U(od.DeviceName, d8, sizeof d8);
          P("    output %u: %s %ldx%ld rotation %d attached %d\n", j, d8,
            od.DesktopCoordinates.right - od.DesktopCoordinates.left,
            od.DesktopCoordinates.bottom - od.DesktopCoordinates.top, od.Rotation, od.AttachedToDesktop);
        }
        IDXGIOutput_Release(o);
      }
      if (i == 0) {
        W2U(d.Description, d8, sizeof d8);
        P("SUMMARY: GPU adapter 0 \"%s\" vendor 0x%04X device 0x%04X%s, driver %u.%u.%u.%u\n", d8, d.VendorId, d.DeviceId,
          basic ? " = MICROSOFT BASIC RENDER DRIVER (no real GPU driver)" : "",
          SUCCEEDED(ch) ? HIWORD(umd.HighPart) : 0, SUCCEEDED(ch) ? LOWORD(umd.HighPart) : 0,
          SUCCEEDED(ch) ? HIWORD(umd.LowPart) : 0, SUCCEEDED(ch) ? LOWORD(umd.LowPart) : 0);
      }
    }
    IDXGIAdapter1_Release(a1);
  }
  IDXGIFactory1_Release(f);
}

// ------------------------------------------------------------------------------------------------ D3DCompile at run time
typedef HRESULT(WINAPI* D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                      ID3DBlob**);
static D3DCompile_t pD3DCompile;
static void load_compiler(void) {
  wchar_t dir[MAX_PATH], path[MAX_PATH + 32];
  GetModuleFileNameW(NULL, dir, MAX_PATH);
  wchar_t* s = wcsrchr(dir, L'\\');
  if (s) *s = 0;
  swprintf(path, MAX_PATH + 32, L"%ls\\d3dcompiler_47.dll", dir);
  HMODULE m = LoadLibraryW(path);
  const char* where = "kit folder";
  if (!m) { m = LoadLibraryW(L"d3dcompiler_47.dll"); where = "system"; }
  pD3DCompile = m ? (D3DCompile_t)(void*)GetProcAddress(m, "D3DCompile") : NULL;
  P("  d3dcompiler_47.dll: %s\n", pD3DCompile ? where : "NOT available (shader tests skipped)");
}
static ID3DBlob* compile(const char* src, const char* profile, char* err, size_t errcap) {
  if (!pD3DCompile) { snprintf(err, errcap, "no compiler"); return NULL; }
  ID3DBlob *b = NULL, *e = NULL;
  HRESULT hr = pD3DCompile(src, strlen(src), "probe", NULL, NULL, "main", profile, 0, 0, &b, &e);
  if (FAILED(hr) || !b) {
    snprintf(err, errcap, "0x%08lX %s", (unsigned long)hr, e ? (const char*)ID3D10Blob_GetBufferPointer(e) : "");
    char* nl = strchr(err, '\n');
    if (nl) *nl = 0;
    if (e) ID3D10Blob_Release(e);
    if (b) ID3D10Blob_Release(b);
    return NULL;
  }
  if (e) ID3D10Blob_Release(e);
  err[0] = 0;
  return b;
}
// a pixel shader with n dependent mad ops ~ n arithmetic slots (for the instruction-slot limit tests)
static char* make_big_ps(int n, int sm4) {
  size_t cap = 256 + (size_t)n * 48;
  char* buf = (char*)malloc(cap);
  if (!buf) return NULL;
  int o = 0;
  if (sm4)
    o += snprintf(buf + o, cap - o, "Texture2D t:register(t0);SamplerState s:register(s0);\n"
                                    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{float4 c=t.Sample(s,uv);\n");
  else
    o += snprintf(buf + o, cap - o, "sampler2D s:register(s0);\nfloat4 main(float2 uv:TEXCOORD0):COLOR{float4 c=tex2D(s,uv);\n");
  for (int i = 0; i < n; ++i) o += snprintf(buf + o, cap - o, "c=mad(c,c.yzwx,c.wzyx+%d.0%d);\n", i % 9, i % 7);
  snprintf(buf + o, cap - o, "return c;}\n");
  return buf;
}

// ------------------------------------------------------------------------------------------------ D3D11
typedef HRESULT(WINAPI* D3D11CreateDevice_t)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                             UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
static D3D11CreateDevice_t load_d3d11(void) {
  HMODULE m = LoadLibraryW(L"d3d11.dll");  // NEVER freed: live devices run code from it (v1's crash)
  return m ? (D3D11CreateDevice_t)(void*)GetProcAddress(m, "D3D11CreateDevice") : NULL;
}
static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                            D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                            D3D_FEATURE_LEVEL_9_1};

static void fmt11(ID3D11Device* dev, const char* name, DXGI_FORMAT fmt) {
  UINT s = 0;
  HRESULT hr = ID3D11Device_CheckFormatSupport(dev, fmt, &s);
  if (FAILED(hr)) { P("    %-10s not supported (0x%08lX)\n", name, (unsigned long)hr); return; }
  P("    %-10s%s%s%s%s%s%s%s%s%s%s%s\n", name, (s & D3D11_FORMAT_SUPPORT_TEXTURE2D) ? " tex2d" : "",
    (s & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ? " rt" : "", (s & D3D11_FORMAT_SUPPORT_BLENDABLE) ? " blend" : "",
    (s & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ? " sample" : "", (s & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) ? " mipgen" : "",
    (s & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) ? " depth" : "", (s & D3D11_FORMAT_SUPPORT_DISPLAY) ? " display" : "",
    (s & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) ? " msaa_rt" : "",
    (s & D3D11_FORMAT_SUPPORT_DECODER_OUTPUT) ? " decoder_out" : "",
    (s & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) ? " vp_in" : "",
    (s & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT) ? " vp_out" : "");
}

static void d3d11_caps(ID3D11Device* dev) {
  D3D11_FEATURE_DATA_THREADING th;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_THREADING, &th, sizeof th)))
    P("  threading: concurrent creates %d, driver command lists %d\n", th.DriverConcurrentCreates, th.DriverCommandLists);
  D3D11_FEATURE_DATA_ARCHITECTURE_INFO ai;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_ARCHITECTURE_INFO, &ai, sizeof ai)))
    P("  tile-based deferred renderer: %d\n", ai.TileBasedDeferredRenderer);
  D3D11_FEATURE_DATA_D3D9_OPTIONS o9;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D9_OPTIONS, &o9, sizeof o9)))
    P("  D3D9 options: full non-pow2 textures %d\n", o9.FullNonPow2TextureSupport);
  D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT sh;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D9_SHADOW_SUPPORT, &sh, sizeof sh)))
    P("  D3D9 shadow (depth as texture): %d\n", sh.SupportsDepthAsTextureWithLessEqualComparisonFilter);
  D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT si;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT, &si, sizeof si)))
    P("  D3D9 simple instancing: %d\n", si.SimpleInstancingSupported);
  D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT mp;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &mp, sizeof mp)))
    P("  min precision: pixel 0x%X, all other stages 0x%X\n", mp.PixelShaderMinPrecision, mp.AllOtherShaderStagesMinPrecision);
  D3D11_FEATURE_DATA_D3D11_OPTIONS op;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D11_OPTIONS, &op, sizeof op)))
    P("  D3D11 options: logic ops %d, NO_OVERWRITE on dynamic CB %d, partial CB updates %d, extended doubles %d\n",
      op.OutputMergerLogicOp, op.MapNoOverwriteOnDynamicConstantBuffer, op.ConstantBufferPartialUpdate,
      op.ExtendedDoublesShaderInstructions);
}

// largest square BGRA (or RGBA) texture that really creates
static void d3d11_max_texture(ID3D11Device* dev, DXGI_FORMAT fmt) {
  UINT best = 0;
  static const UINT sizes[] = {2048, 4096, 8192, 16384};
  for (int i = 0; i < 4; ++i) {
    D3D11_TEXTURE2D_DESC td = {sizes[i], sizes[i], 1, 1, fmt, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ID3D11Texture2D* t = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &t);
    if (SUCCEEDED(hr) && t) { best = sizes[i]; ID3D11Texture2D_Release(t); } else break;
  }
  P("  largest texture that creates: %u x %u\n", best, best);
}

// draw a full-viewport triangle in a known colour into a 64x64 target and read it back
static int d3d11_draw_test(ID3D11Device* dev, ID3D11DeviceContext* ctx, DXGI_FORMAT fmt, const char* tag) {
  char err[512];
  const char* vs_src = "float4 main(float2 p:POSITION):SV_Position{return float4(p,0,1);}";
  const char* ps_src = "float4 main(float4 p:SV_Position):SV_Target{return float4(" DRAW_COLOR ");}";
  ID3DBlob* vsb = compile(vs_src, "vs_4_0_level_9_1", err, sizeof err);
  if (!vsb) { P("  draw test (%s): vs compile failed: %s\n", tag, err); return 0; }
  ID3DBlob* psb = compile(ps_src, "ps_4_0_level_9_1", err, sizeof err);
  if (!psb) { P("  draw test (%s): ps compile failed: %s\n", tag, err); ID3D10Blob_Release(vsb); return 0; }
  ID3D11VertexShader* vs = NULL;
  ID3D11PixelShader* ps = NULL;
  ID3D11InputLayout* il = NULL;
  ID3D11Buffer* vb = NULL;
  ID3D11Texture2D *rt = NULL, *st = NULL;
  ID3D11RenderTargetView* rtv = NULL;
  int ok = 0;
  HRESULT hr = ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (FAILED(hr)) { P("  draw test (%s): CreateVertexShader 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  hr = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &ps);
  if (FAILED(hr)) { P("  draw test (%s): CreatePixelShader 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  D3D11_INPUT_ELEMENT_DESC ie = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
  hr = ID3D11Device_CreateInputLayout(dev, &ie, 1, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), &il);
  if (FAILED(hr)) { P("  draw test (%s): CreateInputLayout 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  float verts[6] = {-1.f, -1.f, -1.f, 3.f, 3.f, -1.f};
  D3D11_BUFFER_DESC bd = {sizeof verts, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA sd = {verts, 0, 0};
  hr = ID3D11Device_CreateBuffer(dev, &bd, &sd, &vb);
  if (FAILED(hr)) { P("  draw test (%s): CreateBuffer 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  D3D11_TEXTURE2D_DESC td = {64, 64, 1, 1, fmt, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &rt);
  if (FAILED(hr)) { P("  draw test (%s): render target 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  hr = ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource*)rt, NULL, &rtv);
  if (FAILED(hr)) { P("  draw test (%s): RTV 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &st);
  if (FAILED(hr)) { P("  draw test (%s): staging 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  float black[4] = {0, 0, 0, 1};
  ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
  D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
  ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  UINT stride = 8, off = 0;
  ID3D11DeviceContext_IASetInputLayout(ctx, il);
  ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &off);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_Draw(ctx, 3, 0);
  ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource*)st, (ID3D11Resource*)rt);
  D3D11_MAPPED_SUBRESOURCE m;
  hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource*)st, 0, D3D11_MAP_READ, 0, &m);
  if (FAILED(hr)) { P("  draw test (%s): Map 0x%08lX\n", tag, (unsigned long)hr); goto done; }
  {
    const BYTE* c = (const BYTE*)m.pData + 32 * m.RowPitch + 32 * 4;
    const BYTE* z = (const BYTE*)m.pData + 2 * m.RowPitch + 61 * 4;
    int r = fmt == DXGI_FORMAT_B8G8R8A8_UNORM ? c[2] : c[0], b = fmt == DXGI_FORMAT_B8G8R8A8_UNORM ? c[0] : c[2];
    int r2 = fmt == DXGI_FORMAT_B8G8R8A8_UNORM ? z[2] : z[0];
    ok = abs(r - 64) <= 3 && abs(c[1] - 128) <= 3 && abs(b - 191) <= 3 && abs(r2 - 64) <= 3;
    P("  draw test (%s): centre pixel R%d G%d B%d A%d, corner R%d (expected 64/128/191/255) -> %s\n", tag, r, c[1], b, c[3],
      r2, ok ? "CORRECT" : "WRONG");
  }
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource*)st, 0);
done:
  if (rtv) ID3D11RenderTargetView_Release(rtv);
  if (st) ID3D11Texture2D_Release(st);
  if (rt) ID3D11Texture2D_Release(rt);
  if (vb) ID3D11Buffer_Release(vb);
  if (il) ID3D11InputLayout_Release(il);
  if (ps) ID3D11PixelShader_Release(ps);
  if (vs) ID3D11VertexShader_Release(vs);
  ID3D10Blob_Release(psb);
  ID3D10Blob_Release(vsb);
  return ok;
}

static void d3d11_shader_tests(ID3D11Device* dev, D3D_FEATURE_LEVEL fl) {
  const char* vs = "float4 main(float2 p:POSITION):SV_Position{return float4(p,0,1);}";
  const char* ps = "float4 main(float4 p:SV_Position):SV_Target{return float4(1,0,0,1);}";
  static const char* vprof[] = {"vs_4_0_level_9_1", "vs_4_0_level_9_3", "vs_4_0", "vs_5_0"};
  static const char* pprof[] = {"ps_4_0_level_9_1", "ps_4_0_level_9_3", "ps_4_0", "ps_5_0"};
  static const D3D_FEATURE_LEVEL need[] = {D3D_FEATURE_LEVEL_9_1, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_10_0,
                                           D3D_FEATURE_LEVEL_11_0};
  char err[512];
  for (int i = 0; i < 4; ++i) {
    if (fl < need[i]) { P("  shaders %s / %s: above this feature level\n", vprof[i], pprof[i]); continue; }
    ID3DBlob* vb = compile(vs, vprof[i], err, sizeof err);
    ID3DBlob* pb = compile(ps, pprof[i], err, sizeof err);
    HRESULT h1 = E_FAIL, h2 = E_FAIL;
    if (vb) {
      ID3D11VertexShader* v = NULL;
      h1 = ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vb), ID3D10Blob_GetBufferSize(vb), NULL, &v);
      if (v) ID3D11VertexShader_Release(v);
      ID3D10Blob_Release(vb);
    }
    if (pb) {
      ID3D11PixelShader* p = NULL;
      h2 = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(pb), ID3D10Blob_GetBufferSize(pb), NULL, &p);
      if (p) ID3D11PixelShader_Release(p);
      ID3D10Blob_Release(pb);
    }
    P("  shaders %s / %s: create 0x%08lX / 0x%08lX\n", vprof[i], pprof[i], (unsigned long)h1, (unsigned long)h2);
  }
  if (fl >= D3D_FEATURE_LEVEL_9_3) {  // a ~330-slot pixel shader (Skia-sized program): over ps_2_0's 64, inside 9_3's 512
    char* big = make_big_ps(170, 1);
    ID3DBlob* b = big ? compile(big, "ps_4_0_level_9_3", err, sizeof err) : NULL;
    if (b) {
      ID3D11PixelShader* p = NULL;
      HRESULT h = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(b), ID3D10Blob_GetBufferSize(b), NULL, &p);
      P("  330-slot ps_4_0_level_9_3: create 0x%08lX\n", (unsigned long)h);
      if (p) ID3D11PixelShader_Release(p);
      ID3D10Blob_Release(b);
    } else {
      P("  330-slot ps_4_0_level_9_3: compile failed %s\n", err);
    }
    free(big);
  }
}

static LRESULT CALLBACK probe_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static HWND probe_window(int w, int h) {
  static ATOM atom;
  if (!atom) {
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = probe_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"VenetiumGpuProbe";
    wc.style = CS_OWNDC;
    atom = RegisterClassW(&wc);
  }
  return CreateWindowExW(0, L"VenetiumGpuProbe", L"gpuprobe", WS_POPUP, 0, 0, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
}

static void d3d11_swapchain_tests(ID3D11Device* dev) {
  IDXGIDevice* xd = NULL;
  IDXGIAdapter* ad = NULL;
  IDXGIFactory2* f2 = NULL;
  if (FAILED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void**)&xd)) || !xd) { P("  swapchain: no IDXGIDevice\n"); return; }
  if (FAILED(IDXGIDevice_GetAdapter(xd, &ad)) || !ad) { P("  swapchain: no adapter\n"); IDXGIDevice_Release(xd); return; }
  if (FAILED(IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory2, (void**)&f2)) || !f2) {
    P("  swapchain: no IDXGIFactory2 (DXGI 1.2)\n");
  } else {
    static const struct { DXGI_SWAP_EFFECT e; UINT n; const char* name; } eff[] = {
      {DXGI_SWAP_EFFECT_DISCARD, 1, "DISCARD (bitblt)"}, {DXGI_SWAP_EFFECT_SEQUENTIAL, 1, "SEQUENTIAL (bitblt)"},
      {DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 2, "FLIP_SEQUENTIAL"}};
    for (int i = 0; i < 3; ++i) {
      HWND w = probe_window(256, 256);
      DXGI_SWAP_CHAIN_DESC1 d;
      ZeroMemory(&d, sizeof d);
      d.Width = 256; d.Height = 256; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
      d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = eff[i].n; d.SwapEffect = eff[i].e;
      IDXGISwapChain1* sc = NULL;
      HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(f2, (IUnknown*)dev, w, &d, NULL, NULL, &sc);
      HRESULT pr = E_FAIL;
      if (SUCCEEDED(hr) && sc) { pr = IDXGISwapChain1_Present(sc, 0, 0); IDXGISwapChain1_Release(sc); }
      P("  swapchain BGRA %s: create 0x%08lX, present 0x%08lX\n", eff[i].name, (unsigned long)hr, (unsigned long)pr);
      if (w) DestroyWindow(w);
    }
    IDXGIFactory2_Release(f2);
  }
  // DirectComposition (Chromium composites through it on Windows 8+)
  HMODULE dc = LoadLibraryW(L"dcomp.dll");
  typedef HRESULT(WINAPI * DCompCreate_t)(IDXGIDevice*, REFIID, void**);
  DCompCreate_t pDC = dc ? (DCompCreate_t)(void*)GetProcAddress(dc, "DCompositionCreateDevice") : NULL;
  if (!pDC) {
    P("  DirectComposition: dcomp.dll / DCompositionCreateDevice not available\n");
  } else {
    IUnknown* cd = NULL;
    HRESULT hr = pDC(xd, &IID_IDCompositionDevice, (void**)&cd);
    P("  DirectComposition device: 0x%08lX\n", (unsigned long)hr);
    if (cd) IUnknown_Release(cd);
  }
  IDXGIAdapter_Release(ad);
  IDXGIDevice_Release(xd);
}

static void section_d3d11(void) {
  S("Direct3D 11 (hardware)");
  load_compiler();
  D3D11CreateDevice_t pCreate = load_d3d11();
  if (!pCreate) { P("  D3D11CreateDevice not available\n"); P("SUMMARY: D3D11 not available\n"); return; }
  P("  per feature level (no flags / BGRA_SUPPORT as Chromium creates it):\n");
  for (int i = 0; i < 7; ++i) {
    HRESULT h[2];
    for (int f = 0; f < 2; ++f) {
      ID3D11Device* d = NULL;
      h[f] = pCreate(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, f ? D3D11_CREATE_DEVICE_BGRA_SUPPORT : 0, &kLevels[i], 1,
                     D3D11_SDK_VERSION, &d, NULL, NULL);
      if (d) ID3D11Device_Release(d);
    }
    P("    %-4s -> 0x%08lX / 0x%08lX\n", fl_name(kLevels[i]), (unsigned long)h[0], (unsigned long)h[1]);
  }
  ID3D11Device* dev = NULL;
  ID3D11DeviceContext* ctx = NULL;
  D3D_FEATURE_LEVEL got = 0;
  HRESULT hr = pCreate(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels, 7, D3D11_SDK_VERSION,
                       &dev, &got, &ctx);
  if (FAILED(hr) || !dev) {
    P("  full list with BGRA_SUPPORT: FAILED 0x%08lX\n", (unsigned long)hr);
    P("SUMMARY: D3D11 hardware device FAILED 0x%08lX -> Venetium 0045 would pick ANGLE D3D9\n", (unsigned long)hr);
    return;
  }
  P("  full list with BGRA_SUPPORT -> feature level %s\n", fl_name(got));
  P("SUMMARY: D3D11 hardware feature level %s -> Venetium 0045 picks ANGLE %s; GPU raster list entry 122 needs ps 5.0 "
    "(FL 11_0+) or a vendor exception: %s\n", fl_name(got), got >= D3D_FEATURE_LEVEL_9_3 ? "D3D11" : "D3D9",
    got >= D3D_FEATURE_LEVEL_11_0 ? "met" : "NOT met");
  TRYSTEP("caps", d3d11_caps(dev));
  P("  format support:\n");
  static const struct { DXGI_FORMAT f; const char* n; } fmts[] = {
    {DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8"}, {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8"}, {DXGI_FORMAT_B8G8R8X8_UNORM, "BGRX8"},
    {DXGI_FORMAT_R10G10B10A2_UNORM, "RGB10A2"}, {DXGI_FORMAT_R16G16B16A16_FLOAT, "RGBA16F"},
    {DXGI_FORMAT_R32G32B32A32_FLOAT, "RGBA32F"}, {DXGI_FORMAT_R16_FLOAT, "R16F"}, {DXGI_FORMAT_R32_FLOAT, "R32F"},
    {DXGI_FORMAT_R8_UNORM, "R8"}, {DXGI_FORMAT_R8G8_UNORM, "RG8"}, {DXGI_FORMAT_A8_UNORM, "A8"},
    {DXGI_FORMAT_B5G6R5_UNORM, "B5G6R5"}, {DXGI_FORMAT_B4G4R4A4_UNORM, "BGRA4"}, {DXGI_FORMAT_D24_UNORM_S8_UINT, "D24S8"},
    {DXGI_FORMAT_D16_UNORM, "D16"}, {DXGI_FORMAT_D32_FLOAT, "D32F"}, {DXGI_FORMAT_BC1_UNORM, "BC1"},
    {DXGI_FORMAT_BC3_UNORM, "BC3"}, {DXGI_FORMAT_NV12, "NV12"}, {DXGI_FORMAT_YUY2, "YUY2"}, {DXGI_FORMAT_P010, "P010"}};
  for (size_t i = 0; i < sizeof fmts / sizeof fmts[0]; ++i) TRYSTEP("format", fmt11(dev, fmts[i].n, fmts[i].f));
  TRYSTEP("max texture", d3d11_max_texture(dev, DXGI_FORMAT_B8G8R8A8_UNORM));
  TRYSTEP("shaders", d3d11_shader_tests(dev, got));
  int ok_bgra = 0, ok_rgba = 0;
  TRYSTEP("draw BGRA", ok_bgra = d3d11_draw_test(dev, ctx, DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8"));
  TRYSTEP("draw RGBA", ok_rgba = d3d11_draw_test(dev, ctx, DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8"));
  P("SUMMARY: D3D11 draw test: BGRA %s, RGBA %s\n", ok_bgra ? "correct" : "WRONG/failed", ok_rgba ? "correct" : "WRONG/failed");
  TRYSTEP("swapchain", d3d11_swapchain_tests(dev));
  // devices are released by process exit (some software adapters fast-fail on teardown)
}

// ------------------------------------------------------------------------------------------------ D3D11 video
static void section_d3d11video(void) {
  S("Direct3D 11 video (hardware decode and video processor)");
#ifdef PROBE_NEGATIVE_CONTROL
  P("  negative control: fast fail now\n");
  __fastfail(7);
#endif
  D3D11CreateDevice_t pCreate = load_d3d11();
  if (!pCreate) { P("  D3D11CreateDevice not available\n"); return; }
  ID3D11Device* dev = NULL;
  ID3D11DeviceContext* ctx = NULL;
  D3D_FEATURE_LEVEL got = 0;
  HRESULT hr = pCreate(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                       kLevels, 7, D3D11_SDK_VERSION, &dev, &got, &ctx);
  if (FAILED(hr) || !dev) {
    P("  D3D11 device with VIDEO_SUPPORT: FAILED 0x%08lX (no D3D11 video on this driver)\n", (unsigned long)hr);
    P("SUMMARY: D3D11 video: none (VIDEO_SUPPORT device 0x%08lX)\n", (unsigned long)hr);
    return;
  }
  P("  device with VIDEO_SUPPORT: feature level %s\n", fl_name(got));
  ID3D11VideoDevice* vd = NULL;
  hr = ID3D11Device_QueryInterface(dev, &IID_ID3D11VideoDevice, (void**)&vd);
  if (FAILED(hr) || !vd) { P("  ID3D11VideoDevice: 0x%08lX\n", (unsigned long)hr); P("SUMMARY: D3D11 video: no ID3D11VideoDevice\n"); return; }
  UINT n = ID3D11VideoDevice_GetVideoDecoderProfileCount(vd);
  P("  %u decoder profiles:\n", n);
  int h264 = 0;
  char gs[64];
  for (UINT i = 0; i < n; ++i) {
    GUID g;
    if (FAILED(ID3D11VideoDevice_GetVideoDecoderProfile(vd, i, &g))) continue;
    BOOL nv12 = FALSE;
    ID3D11VideoDevice_CheckVideoDecoderFormat(vd, &g, DXGI_FORMAT_NV12, &nv12);
    guid_str(&g, gs, sizeof gs);
    P("    %s %s, NV12 output %d\n", gs, guid_name(&g), nv12);
    if (IsEqualGUID(&g, &kH264)) h264 = 1;
  }
  if (h264) {
    static const UINT wh[][2] = {{1280, 720}, {1920, 1088}, {3840, 2160}};
    for (int i = 0; i < 3; ++i) {
      D3D11_VIDEO_DECODER_DESC desc = {kH264, wh[i][0], wh[i][1], DXGI_FORMAT_NV12};
      UINT cfg = 0;
      HRESULT ch = ID3D11VideoDevice_GetVideoDecoderConfigCount(vd, &desc, &cfg);
      P("    H.264 %ux%u: config count %u (0x%08lX)\n", wh[i][0], wh[i][1], cfg, (unsigned long)ch);
    }
  }
  P("SUMMARY: D3D11 video: %u decoder profiles, H.264 %s\n", n, h264 ? "YES" : "no");
  // video processor (NV12 -> BGRA conversion and scaling, what Chromium's D3D11 video path uses)
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd;
  ZeroMemory(&cd, sizeof cd);
  cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  cd.InputFrameRate.Numerator = 30; cd.InputFrameRate.Denominator = 1;
  cd.InputWidth = 1280; cd.InputHeight = 720;
  cd.OutputFrameRate.Numerator = 30; cd.OutputFrameRate.Denominator = 1;
  cd.OutputWidth = 1280; cd.OutputHeight = 720;
  cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  ID3D11VideoProcessorEnumerator* en = NULL;
  hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator(vd, &cd, &en);
  if (FAILED(hr) || !en) {
    P("  video processor enumerator: 0x%08lX\n", (unsigned long)hr);
  } else {
    D3D11_VIDEO_PROCESSOR_CAPS c;
    if (SUCCEEDED(ID3D11VideoProcessorEnumerator_GetVideoProcessorCaps(en, &c)))
      P("  video processor: device caps 0x%X, feature caps 0x%X, filter caps 0x%X, input format caps 0x%X, max input "
        "streams %u, rate conversion caps %u\n", c.DeviceCaps, c.FeatureCaps, c.FilterCaps, c.InputFormatCaps,
        c.MaxInputStreams, c.RateConversionCapsCount);
    static const struct { DXGI_FORMAT f; const char* n; } vf[] = {
      {DXGI_FORMAT_NV12, "NV12"}, {DXGI_FORMAT_YUY2, "YUY2"}, {DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8"},
      {DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8"}};
    for (int i = 0; i < 4; ++i) {
      UINT fl = 0;
      HRESULT fh = ID3D11VideoProcessorEnumerator_CheckVideoProcessorFormat(en, vf[i].f, &fl);
      P("    VP format %-5s: input %d output %d (0x%08lX)\n", vf[i].n, !!(fl & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT),
        !!(fl & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT), (unsigned long)fh);
    }
    ID3D11VideoProcessorEnumerator_Release(en);
  }
  ID3D11VideoDevice_Release(vd);
}

// ------------------------------------------------------------------------------------------------ D3D9 (+ DXVA2)
typedef HRESULT(WINAPI* Create9Ex_t)(UINT, IDirect3D9Ex**);
static IDirect3D9Ex* make_d3d9(void) {
  HMODULE m = LoadLibraryW(L"d3d9.dll");  // kept loaded
  Create9Ex_t p = m ? (Create9Ex_t)(void*)GetProcAddress(m, "Direct3DCreate9Ex") : NULL;
  IDirect3D9Ex* d = NULL;
  if (!p) { P("  Direct3DCreate9Ex not available\n"); return NULL; }
  HRESULT hr = p(D3D_SDK_VERSION, &d);
  if (FAILED(hr) || !d) { P("  Direct3DCreate9Ex failed 0x%08lX\n", (unsigned long)hr); return NULL; }
  return d;
}
static IDirect3DDevice9Ex* make_d3d9_device(IDirect3D9Ex* d3d, HWND* out_wnd) {
  HWND w = probe_window(64, 64);
  *out_wnd = w;
  D3DPRESENT_PARAMETERS pp;
  ZeroMemory(&pp, sizeof pp);
  pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.hDeviceWindow = w;
  IDirect3DDevice9Ex* dev = NULL;
  HRESULT hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w,
                                           D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED,
                                           &pp, NULL, &dev);
  P("  CreateDeviceEx(HAL, hardware vertex processing): 0x%08lX\n", (unsigned long)hr);
  if (FAILED(hr) || !dev) {
    hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &pp,
                                     NULL, &dev);
    P("  CreateDeviceEx(HAL, software vertex processing): 0x%08lX\n", (unsigned long)hr);
  }
  return SUCCEEDED(hr) ? dev : NULL;
}

static void d3d9_formats(IDirect3D9Ex* d3d) {
  static const struct { D3DFORMAT f; DWORD usage; D3DRESOURCETYPE rt; const char* n; } t[] = {
    {D3DFMT_A8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, "A8R8G8B8 rt-texture"},
    {D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, "X8R8G8B8 rt-texture"},
    {D3DFMT_A16B16G16R16F, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, "A16B16G16R16F rt-texture"},
    {D3DFMT_R16F, 0, D3DRTYPE_TEXTURE, "R16F texture"},
    {D3DFMT_A8, 0, D3DRTYPE_TEXTURE, "A8 texture"},
    {D3DFMT_L8, 0, D3DRTYPE_TEXTURE, "L8 texture"},
    {D3DFMT_A8L8, 0, D3DRTYPE_TEXTURE, "A8L8 texture"},
    {D3DFMT_R5G6B5, 0, D3DRTYPE_TEXTURE, "R5G6B5 texture"},
    {D3DFMT_DXT1, 0, D3DRTYPE_TEXTURE, "DXT1 texture"},
    {D3DFMT_DXT5, 0, D3DRTYPE_TEXTURE, "DXT5 texture"},
    {D3DFMT_D24S8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, "D24S8 depth"},
    {D3DFMT_D16, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, "D16 depth"},
    {(D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, "INTZ depth-texture"},
    {(D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L'), D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, "NULL rt"},
  };
  P("  formats (CheckDeviceFormat vs X8R8G8B8 display):\n");
  for (size_t i = 0; i < sizeof t / sizeof t[0]; ++i) {
    HRESULT hr = IDirect3D9Ex_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, t[i].usage, t[i].rt, t[i].f);
    P("    %-26s %s\n", t[i].n, SUCCEEDED(hr) ? "yes" : "no");
  }
  HRESULT cv = IDirect3D9Ex_CheckDeviceFormatConversion(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                                        (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2'), D3DFMT_X8R8G8B8);
  P("    NV12 -> X8R8G8B8 StretchRect   %s\n", SUCCEEDED(cv) ? "yes" : "no");
}

static int d3d9_draw_test(IDirect3DDevice9Ex* dev) {
  char err[512];
  ID3DBlob* vsb = compile("float4 main(float2 p:POSITION):POSITION{return float4(p,0,1);}", "vs_2_0", err, sizeof err);
  ID3DBlob* psb = compile("float4 main():COLOR{return float4(" DRAW_COLOR ");}", "ps_2_0", err, sizeof err);
  if (!vsb || !psb) { P("  D3D9 draw test: shader compile failed %s\n", err); if (vsb) ID3D10Blob_Release(vsb); if (psb) ID3D10Blob_Release(psb); return 0; }
  IDirect3DVertexShader9* vs = NULL;
  IDirect3DPixelShader9* ps = NULL;
  IDirect3DVertexDeclaration9* decl = NULL;
  IDirect3DSurface9 *rt = NULL, *sys = NULL;
  int ok = 0;
  HRESULT hr = IDirect3DDevice9Ex_CreateVertexShader(dev, (const DWORD*)ID3D10Blob_GetBufferPointer(vsb), &vs);
  if (FAILED(hr)) { P("  D3D9 draw test: CreateVertexShader 0x%08lX\n", (unsigned long)hr); goto done; }
  hr = IDirect3DDevice9Ex_CreatePixelShader(dev, (const DWORD*)ID3D10Blob_GetBufferPointer(psb), &ps);
  if (FAILED(hr)) { P("  D3D9 draw test: CreatePixelShader 0x%08lX\n", (unsigned long)hr); goto done; }
  D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
  hr = IDirect3DDevice9Ex_CreateVertexDeclaration(dev, el, &decl);
  if (FAILED(hr)) { P("  D3D9 draw test: CreateVertexDeclaration 0x%08lX\n", (unsigned long)hr); goto done; }
  hr = IDirect3DDevice9Ex_CreateRenderTarget(dev, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, NULL);
  if (FAILED(hr)) { P("  D3D9 draw test: CreateRenderTarget 0x%08lX\n", (unsigned long)hr); goto done; }
  hr = IDirect3DDevice9Ex_CreateOffscreenPlainSurface(dev, 64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, NULL);
  if (FAILED(hr)) { P("  D3D9 draw test: system surface 0x%08lX\n", (unsigned long)hr); goto done; }
  IDirect3DDevice9Ex_SetRenderTarget(dev, 0, rt);
  IDirect3DDevice9Ex_Clear(dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
  IDirect3DDevice9Ex_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
  IDirect3DDevice9Ex_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
  IDirect3DDevice9Ex_SetVertexDeclaration(dev, decl);
  IDirect3DDevice9Ex_SetVertexShader(dev, vs);
  IDirect3DDevice9Ex_SetPixelShader(dev, ps);
  float verts[6] = {-1.f, -1.f, -1.f, 3.f, 3.f, -1.f};
  IDirect3DDevice9Ex_BeginScene(dev);
  hr = IDirect3DDevice9Ex_DrawPrimitiveUP(dev, D3DPT_TRIANGLELIST, 1, verts, 8);
  IDirect3DDevice9Ex_EndScene(dev);
  if (FAILED(hr)) { P("  D3D9 draw test: DrawPrimitiveUP 0x%08lX\n", (unsigned long)hr); goto done; }
  hr = IDirect3DDevice9Ex_GetRenderTargetData(dev, rt, sys);
  if (FAILED(hr)) { P("  D3D9 draw test: GetRenderTargetData 0x%08lX\n", (unsigned long)hr); goto done; }
  D3DLOCKED_RECT lr;
  if (SUCCEEDED(IDirect3DSurface9_LockRect(sys, &lr, NULL, D3DLOCK_READONLY))) {
    const BYTE* c = (const BYTE*)lr.pBits + 32 * lr.Pitch + 32 * 4;  // A8R8G8B8 = B,G,R,A bytes
    ok = abs(c[2] - 64) <= 3 && abs(c[1] - 128) <= 3 && abs(c[0] - 191) <= 3;
    P("  D3D9 draw test: centre pixel R%d G%d B%d A%d (expected 64/128/191/255) -> %s\n", c[2], c[1], c[0], c[3],
      ok ? "CORRECT" : "WRONG");
    IDirect3DSurface9_UnlockRect(sys);
  }
done:
  if (sys) IDirect3DSurface9_Release(sys);
  if (rt) IDirect3DSurface9_Release(rt);
  if (decl) IDirect3DVertexDeclaration9_Release(decl);
  if (ps) IDirect3DPixelShader9_Release(ps);
  if (vs) IDirect3DVertexShader9_Release(vs);
  ID3D10Blob_Release(psb);
  ID3D10Blob_Release(vsb);
  return ok;
}

static void d3d9_shader_tests(IDirect3DDevice9Ex* dev) {
  char err[512];
  static const char* prof[] = {"ps_2_0", "ps_2_a", "ps_2_b", "ps_3_0"};
  char* big = make_big_ps(170, 0);
  for (int i = 0; i < 4 && big; ++i) {
    ID3DBlob* b = compile(big, prof[i], err, sizeof err);
    if (!b) { P("  330-slot %s: compile rejected (%s)\n", prof[i], err); continue; }
    IDirect3DPixelShader9* ps = NULL;
    HRESULT hr = IDirect3DDevice9Ex_CreatePixelShader(dev, (const DWORD*)ID3D10Blob_GetBufferPointer(b), &ps);
    P("  330-slot %s: CreatePixelShader 0x%08lX%s\n", prof[i], (unsigned long)hr, SUCCEEDED(hr) ? " (driver accepts it)" : "");
    if (ps) IDirect3DPixelShader9_Release(ps);
    ID3D10Blob_Release(b);
  }
  free(big);
}

static void section_d3d9(void) {
  S("Direct3D 9Ex");
  load_compiler();
  IDirect3D9Ex* d3d = make_d3d9();
  if (!d3d) { P("SUMMARY: D3D9Ex not available\n"); return; }
  P("  adapters: %u\n", IDirect3D9Ex_GetAdapterCount(d3d));
  D3DADAPTER_IDENTIFIER9 id;
  if (SUCCEEDED(IDirect3D9Ex_GetAdapterIdentifier(d3d, D3DADAPTER_DEFAULT, 0, &id))) {
    char gs[64];
    guid_str(&id.DeviceIdentifier, gs, sizeof gs);
    P("  adapter 0: \"%s\" driver %s version %u.%u.%u.%u\n    vendor 0x%04lX device 0x%04lX subsys 0x%08lX rev %lu, "
      "device identifier %s, WHQL %lu\n", id.Description, id.Driver, HIWORD(id.DriverVersion.HighPart),
      LOWORD(id.DriverVersion.HighPart), HIWORD(id.DriverVersion.LowPart), LOWORD(id.DriverVersion.LowPart), id.VendorId,
      id.DeviceId, id.SubSysId, id.Revision, gs, id.WHQLLevel);
  }
  D3DCAPS9 c;
  HRESULT hr = IDirect3D9Ex_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &c);
  if (FAILED(hr)) {
    P("  GetDeviceCaps(HAL): 0x%08lX (no D3D9 hardware driver)\n", (unsigned long)hr);
    P("SUMMARY: D3D9 HAL caps FAILED 0x%08lX\n", (unsigned long)hr);
  } else {
    P("  pixel shader %u.%u, vertex shader %u.%u, PS20 caps 0x%X (slots %d, temps %d), max texture %lux%lu, "
      "simultaneous RTs %lu, max VS constants %lu\n", D3DSHADER_VERSION_MAJOR(c.PixelShaderVersion),
      D3DSHADER_VERSION_MINOR(c.PixelShaderVersion), D3DSHADER_VERSION_MAJOR(c.VertexShaderVersion),
      D3DSHADER_VERSION_MINOR(c.VertexShaderVersion), c.PS20Caps.Caps, c.PS20Caps.NumInstructionSlots, c.PS20Caps.NumTemps,
      c.MaxTextureWidth, c.MaxTextureHeight, c.NumSimultaneousRTs, c.MaxVertexShaderConst);
    P("  TextureCaps 0x%lX RasterCaps 0x%lX DevCaps 0x%lX DevCaps2 0x%lX StretchRectFilterCaps 0x%lX DeclTypes 0x%lX\n",
      c.TextureCaps, c.RasterCaps, c.DevCaps, c.DevCaps2, c.StretchRectFilterCaps, c.DeclTypes);
    P("SUMMARY: D3D9 HAL: pixel shader %u.%u, max texture %lu\n", D3DSHADER_VERSION_MAJOR(c.PixelShaderVersion),
      D3DSHADER_VERSION_MINOR(c.PixelShaderVersion), c.MaxTextureWidth);
  }
  TRYSTEP("formats", d3d9_formats(d3d));
  HWND w = NULL;
  IDirect3DDevice9Ex* dev = NULL;
  TRYSTEP("CreateDeviceEx", dev = make_d3d9_device(d3d, &w));
  if (!dev) { P("SUMMARY: D3D9 device: none\n"); return; }
  int ok = 0;
  TRYSTEP("draw", ok = d3d9_draw_test(dev));
  P("SUMMARY: D3D9 draw test: %s\n", ok ? "correct" : "WRONG/failed");
  TRYSTEP("shaders", d3d9_shader_tests(dev));
}

static void section_dxva2(void) {
  S("DXVA2 (D3D9 hardware video decode - Venetium's H.264 path on Tegra)");
  IDirect3D9Ex* d3d = make_d3d9();
  if (!d3d) return;
  HWND w = NULL;
  IDirect3DDevice9Ex* dev = make_d3d9_device(d3d, &w);
  if (!dev) { P("SUMMARY: DXVA2: no D3D9 device\n"); return; }
  HMODULE m = LoadLibraryW(L"dxva2.dll");
  typedef HRESULT(WINAPI * CreateVS_t)(IDirect3DDevice9*, REFIID, void**);
  CreateVS_t pCreate = m ? (CreateVS_t)(void*)GetProcAddress(m, "DXVA2CreateVideoService") : NULL;
  if (!pCreate) { P("  DXVA2CreateVideoService not available\n"); return; }
  IDirectXVideoDecoderService* svc = NULL;
  HRESULT hr = pCreate((IDirect3DDevice9*)dev, &IID_IDirectXVideoDecoderService, (void**)&svc);
  if (FAILED(hr) || !svc) { P("  decoder service: 0x%08lX\n", (unsigned long)hr); P("SUMMARY: DXVA2: no decoder service\n"); return; }
  UINT n = 0;
  GUID* guids = NULL;
  hr = IDirectXVideoDecoderService_GetDecoderDeviceGuids(svc, &n, &guids);
  P("  decoder GUIDs: %u (0x%08lX)\n", n, (unsigned long)hr);
  int h264 = 0;
  char gs[64];
  for (UINT i = 0; i < n && guids; ++i) {
    guid_str(&guids[i], gs, sizeof gs);
    P("    %s %s\n", gs, guid_name(&guids[i]));
    if (IsEqualGUID(&guids[i], &kH264)) h264 = 1;
  }
  if (guids) CoTaskMemFree(guids);
  if (h264) {
    UINT nf = 0;
    D3DFORMAT* fmts = NULL;
    if (SUCCEEDED(IDirectXVideoDecoderService_GetDecoderRenderTargets(svc, &kH264, &nf, &fmts)) && fmts) {
      P("    H.264 output formats:");
      for (UINT i = 0; i < nf; ++i) {
        DWORD f = (DWORD)fmts[i];
        P(" %c%c%c%c", (char)(f & 0xff), (char)((f >> 8) & 0xff), (char)((f >> 16) & 0xff), (char)((f >> 24) & 0xff));
      }
      P("\n");
      CoTaskMemFree(fmts);
    }
    static const UINT wh[][2] = {{1280, 720}, {1920, 1088}};
    for (int i = 0; i < 2; ++i) {
      DXVA2_VideoDesc vdsc;
      ZeroMemory(&vdsc, sizeof vdsc);
      vdsc.SampleWidth = wh[i][0]; vdsc.SampleHeight = wh[i][1];
      vdsc.Format = (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2');
      vdsc.InputSampleFreq.Numerator = 30; vdsc.InputSampleFreq.Denominator = 1;
      vdsc.OutputFrameFreq = vdsc.InputSampleFreq;
      UINT nc = 0;
      DXVA2_ConfigPictureDecode* cfg = NULL;
      HRESULT ch = IDirectXVideoDecoderService_GetDecoderConfigurations(svc, &kH264, &vdsc, NULL, &nc, &cfg);
      P("    H.264 %ux%u NV12: %u configurations (0x%08lX)\n", wh[i][0], wh[i][1], nc, (unsigned long)ch);
      if (cfg) CoTaskMemFree(cfg);
    }
  }
  P("SUMMARY: DXVA2: %u decoder GUIDs, H.264 %s\n", n, h264 ? "YES" : "no");
  IDirectXVideoDecoderService_Release(svc);
}

// ------------------------------------------------------------------------------------------------ Media Foundation
static void section_mf(void) {
  S("Media Foundation video decoders");
#ifdef PROBE_NEGATIVE_CONTROL
  P("  negative control: hang now\n");
  Sleep(INFINITE);
#endif
  HMODULE m = LoadLibraryW(L"mfplat.dll");
  typedef HRESULT(WINAPI * MFStartup_t)(ULONG, DWORD);
  typedef HRESULT(WINAPI * MFTEnumEx_t)(GUID, UINT32, const MFT_REGISTER_TYPE_INFO*, const MFT_REGISTER_TYPE_INFO*,
                                        IMFActivate***, UINT32*);
  MFStartup_t pStart = m ? (MFStartup_t)(void*)GetProcAddress(m, "MFStartup") : NULL;
  MFTEnumEx_t pEnum = m ? (MFTEnumEx_t)(void*)GetProcAddress(m, "MFTEnumEx") : NULL;
  if (!pStart || !pEnum) { P("  mfplat.dll / MFTEnumEx not available\n"); P("SUMMARY: Media Foundation not available\n"); return; }
  HRESULT hr = pStart(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) { P("  MFStartup 0x%08lX\n", (unsigned long)hr); return; }
  static const struct { const GUID* sub; const char* n; } in[] = {
    {&MFVideoFormat_H264, "H.264"}, {&MFVideoFormat_HEVC, "HEVC"}, {&MFVideoFormat_VP90, "VP9"},
    {&MFVideoFormat_MPEG2, "MPEG-2"}, {&MFVideoFormat_WVC1, "VC-1"}, {&MFVideoFormat_MJPG, "MJPEG"}};
  int h264hw = 0;
  char n8[512];
  for (int i = 0; i < 6; ++i) {
    MFT_REGISTER_TYPE_INFO ti = {MFMediaType_Video, *in[i].sub};
    IMFActivate** acts = NULL;
    UINT32 n = 0;
    hr = pEnum(MFT_CATEGORY_VIDEO_DECODER,
               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_LOCALMFT, &ti, NULL,
               &acts, &n);
    P("  %-6s: %u decoder(s)%s\n", in[i].n, n, FAILED(hr) ? " (enum failed)" : "");
    for (UINT32 j = 0; j < n && acts; ++j) {
      WCHAR* name = NULL;
      UINT32 len = 0, flags = 0;
      IMFActivate_GetAllocatedString(acts[j], &MFT_FRIENDLY_NAME_Attribute, &name, &len);
      IMFActivate_GetUINT32(acts[j], &MF_TRANSFORM_FLAGS_Attribute, &flags);
      WCHAR* url = NULL;
      UINT32 ul = 0;
      int hw = SUCCEEDED(IMFActivate_GetAllocatedString(acts[j], &MFT_ENUM_HARDWARE_URL_Attribute, &url, &ul));
      W2U(name ? name : L"(no name)", n8, sizeof n8);
      P("    \"%s\" flags 0x%X%s%s\n", n8, flags, (flags & MFT_ENUM_FLAG_HARDWARE) || hw ? " HARDWARE" : "",
        (flags & MFT_ENUM_FLAG_ASYNCMFT) ? " async" : "");
      if (i == 0 && ((flags & MFT_ENUM_FLAG_HARDWARE) || hw)) h264hw = 1;
      if (name) CoTaskMemFree(name);
      if (url) CoTaskMemFree(url);
      IMFActivate_Release(acts[j]);
    }
    if (acts) CoTaskMemFree(acts);
  }
  P("SUMMARY: Media Foundation: hardware H.264 MFT %s\n", h264hw ? "YES" : "no (software MFT / DXVA through it possible)");
}

// ------------------------------------------------------------------------------------------------ OpenGL (for completeness)
static void section_opengl(void) {
  S("OpenGL (opengl32.dll)");
  HMODULE gl = LoadLibraryW(L"opengl32.dll");
  if (!gl) { P("  opengl32.dll not present\n"); return; }
  typedef HGLRC(WINAPI * wglCreateContext_t)(HDC);
  typedef BOOL(WINAPI * wglMakeCurrent_t)(HDC, HGLRC);
  typedef const unsigned char*(WINAPI * glGetString_t)(unsigned);
  wglCreateContext_t pCreate = (wglCreateContext_t)(void*)GetProcAddress(gl, "wglCreateContext");
  wglMakeCurrent_t pMake = (wglMakeCurrent_t)(void*)GetProcAddress(gl, "wglMakeCurrent");
  glGetString_t pStr = (glGetString_t)(void*)GetProcAddress(gl, "glGetString");
  if (!pCreate || !pMake || !pStr) { P("  wgl entry points missing\n"); return; }
  HWND w = probe_window(64, 64);
  HDC dc = GetDC(w);
  PIXELFORMATDESCRIPTOR pfd;
  ZeroMemory(&pfd, sizeof pfd);
  pfd.nSize = sizeof pfd; pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24;
  int pf = ChoosePixelFormat(dc, &pfd);
  if (!pf || !SetPixelFormat(dc, pf, &pfd)) { P("  no pixel format\n"); return; }
  HGLRC rc = pCreate(dc);
  if (!rc || !pMake(dc, rc)) { P("  wglCreateContext / MakeCurrent failed\n"); return; }
  const char* v = (const char*)pStr(0x1F00);  // GL_VENDOR
  const char* r = (const char*)pStr(0x1F01);  // GL_RENDERER
  const char* s = (const char*)pStr(0x1F02);  // GL_VERSION
  P("  vendor \"%s\", renderer \"%s\", version \"%s\"\n", v ? v : "?", r ? r : "?", s ? s : "?");
  P("SUMMARY: OpenGL: %s / %s %s\n", v ? v : "?", r ? r : "?", s ? s : "?");
}

// ================================================================================================ runner
typedef struct { const char* name; void (*fn)(void); DWORD timeout_ms; } Section;
static const Section kSections[] = {
  {"system", section_system, 30000},   {"display", section_display, 30000}, {"dxgi", section_dxgi, 30000},
  {"d3d11", section_d3d11, 120000},     {"d3d11video", section_d3d11video, 60000},
  {"d3d9", section_d3d9, 120000},       {"dxva2", section_dxva2, 60000},     {"mf", section_mf, 60000},
  {"opengl", section_opengl, 30000},
};

static int run_section_inline(const char* name) {
  _setmode(_fileno(stdout), _O_BINARY);
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  for (size_t i = 0; i < sizeof kSections / sizeof kSections[0]; ++i)
    if (!strcmp(kSections[i].name, name)) {
      __try { kSections[i].fn(); }
      __except (EXCEPTION_EXECUTE_HANDLER) { P("  [section %s faulted: exception 0x%08lX]\n", name, (unsigned long)GetExceptionCode()); }
      fflush(stdout);
      return 0;
    }
  P("unknown section %s\n", name);
  return 2;
}

static FILE* g_report;
static char* g_summary;
static size_t g_summary_len, g_summary_cap;
static void report_write(const char* s, size_t n) {
  if (g_report) { fwrite(s, 1, n, g_report); fflush(g_report); }
  DWORD w;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)n, &w, NULL);
}
static void R(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n > 0) report_write(buf, (size_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
}
static void summary_add(const char* line, size_t n) {
  if (g_summary_len + n + 2 > g_summary_cap) {
    size_t cap = (g_summary_cap + n + 2) * 2;
    char* p = (char*)realloc(g_summary, cap);
    if (!p) return;
    g_summary = p; g_summary_cap = cap;
  }
  memcpy(g_summary + g_summary_len, line, n);
  g_summary_len += n;
  g_summary[g_summary_len++] = '\n';
}

// writes a chunk of child output to the report and collects its "SUMMARY: " lines (line state kept across chunks)
static char g_line[4096];
static size_t g_ll;
static void child_chunk(const char* buf, DWORD got) {
  report_write(buf, got);
  for (DWORD i = 0; i < got; ++i) {
    if (buf[i] == '\n') {
      if (g_ll >= 9 && !memcmp(g_line, "SUMMARY: ", 9)) summary_add(g_line + 9, g_ll - 9);
      g_ll = 0;
    } else if (buf[i] != '\r' && g_ll < sizeof g_line - 1) {
      g_line[g_ll++] = buf[i];
    }
  }
}

// runs one section in a child process (stdout piped here), with a timeout; a crash or hang loses only that section
static void run_child(const Section* sec) {
  wchar_t exe[MAX_PATH], cmd[MAX_PATH + 64];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  swprintf(cmd, MAX_PATH + 64, L"\"%ls\" --section %hs", exe, sec->name);
  SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
  HANDLE rd = NULL, wr = NULL;
  if (!CreatePipe(&rd, &wr, &sa, 0)) { R("\n== %s ==\n  [could not create a pipe]\n", sec->name); return; }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi;
  DWORD t0 = GetTickCount();
  if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
    R("\n== %s ==\n  [could not start the section process: %lu]\n", sec->name, GetLastError());
    CloseHandle(rd); CloseHandle(wr); return;
  }
  CloseHandle(wr);
  char buf[4096];
  g_ll = 0;
  int timed_out = 0;
  for (;;) {
    DWORD avail = 0;
    if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;  // child closed the pipe
    if (avail) {
      DWORD got = 0;
      if (!ReadFile(rd, buf, avail < sizeof buf ? avail : sizeof buf, &got, NULL) || !got) break;
      child_chunk(buf, got);
      continue;
    }
    if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
      DWORD got = 0;
      while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail &&
             ReadFile(rd, buf, avail < sizeof buf ? avail : sizeof buf, &got, NULL) && got)
        child_chunk(buf, got);
      break;
    }
    if (GetTickCount() - t0 > sec->timeout_ms) { timed_out = 1; TerminateProcess(pi.hProcess, 0xDEAD); break; }
  }
  WaitForSingleObject(pi.hProcess, 5000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  if (timed_out) {
    R("  [section %s HUNG: stopped after %lu s]\n", sec->name, sec->timeout_ms / 1000);
    char s[128]; int n = snprintf(s, sizeof s, "section %s HUNG (driver did not return)", sec->name); summary_add(s, (size_t)n);
  } else if (code != 0) {
    R("  [section %s CRASHED: exit code 0x%08lX after %lu ms]\n", sec->name, code, GetTickCount() - t0);
    char s[128]; int n = snprintf(s, sizeof s, "section %s CRASHED 0x%08lX", sec->name, code); summary_add(s, (size_t)n);
  } else {
    R("  [section %s done in %lu ms]\n", sec->name, GetTickCount() - t0);
  }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
}

static int save_clipboard(const wchar_t* path) {
  if (!OpenClipboard(NULL)) { fwprintf(stderr, L"could not open the clipboard\n"); return 1; }
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  const wchar_t* w = h ? (const wchar_t*)GlobalLock(h) : NULL;
  if (!w) { CloseClipboard(); fwprintf(stderr, L"the clipboard has no text - click \"Copy Report to Clipboard\" first\n"); return 1; }
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char* u = (char*)malloc((size_t)n);
  if (u) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
  GlobalUnlock(h);
  CloseClipboard();
  if (!u) return 1;
  FILE* f = _wfopen(path, L"wb");
  if (!f) { free(u); fwprintf(stderr, L"could not write %ls\n", path); return 1; }
  fwrite(u, 1, strlen(u), f);
  fclose(f);
  wprintf(L"saved %d characters of the clipboard to %ls\n", n - 1, path);
  free(u);
  return n > 200 ? 0 : 3;  // a real chrome://gpu report is long
}

static int clear_clipboard(void) {
  if (!OpenClipboard(NULL)) return 1;
  EmptyClipboard();
  CloseClipboard();
  return 0;
}

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  if (argc >= 2 && !wcscmp(argv[1], L"--clear-clipboard")) return clear_clipboard();
  if (argc >= 3 && !wcscmp(argv[1], L"--section")) {
    char n[64];
    W2U(argv[2], n, sizeof n);
    return run_section_inline(n);
  }
  if (argc >= 3 && !wcscmp(argv[1], L"--save-clipboard")) return save_clipboard(argv[2]);
  wchar_t dir[MAX_PATH];
  if (argc > 1) { wcsncpy(dir, argv[1], MAX_PATH - 1); dir[MAX_PATH - 1] = 0; }
  else { GetModuleFileNameW(NULL, dir, MAX_PATH); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
  CreateDirectoryW(dir, NULL);
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t path[MAX_PATH + 64], latest[MAX_PATH + 64];
  swprintf(path, MAX_PATH + 64, L"%ls\\gpuprobe-%04u%02u%02u-%02u%02u%02u.txt", dir, t.wYear, t.wMonth, t.wDay, t.wHour,
           t.wMinute, t.wSecond);
  swprintf(latest, MAX_PATH + 64, L"%ls\\gpuprobe-report.txt", dir);
  g_report = _wfopen(path, L"wb");
  R("%s\nrun %04u-%02u-%02u %02u:%02u:%02u\n", PROBE_VERSION, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  for (size_t i = 0; i < sizeof kSections / sizeof kSections[0]; ++i) run_child(&kSections[i]);
  R("\n== SUMMARY ==\n");
  if (g_summary) report_write(g_summary, g_summary_len);
  R("\n== done ==\nreport: %ls\n", path);
  if (g_report) fclose(g_report);
  CopyFileW(path, latest, FALSE);
  return 0;
}
