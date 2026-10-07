// d3dprobe.c - Venetium device diagnostics (BATCH-GPU-1 section 2.5): what Direct3D the Surface 2 actually offers.
//
// The "hangs" and 720p problems trace to ANGLE running on D3D9 at shader model 2.0 (ps_2_0, 64 arithmetic slots) on the Tegra.
// This probe measures, on the device, the facts that decide the GPU route: the Direct3D feature level, whether the driver
// accepts the larger ps_2_a / ps_2_b / ps_4_0_level_9_3 profiles (section 2.4 showed those allow 512 arithmetic slots, enough
// for all seven failing Skia programs), and what hardware video decode the device offers. It changes nothing on the machine.
//
// It is C (rt2.1 clang-cl cannot emit Windows ARM32 C++ exceptions, TOOLCHAIN B4), using the D3D headers' C vtable macros
// (COBJMACROS). d3d11.dll, dxgi.dll and d3d9.dll are on Windows 10 15035; d3dcompiler_47.dll is loaded at run time from the
// probe's own folder (shipped in the kit) so it is not a load-time dependency.
//
// Usage: d3dprobe.exe [output-dir]   (default: the folder holding d3dprobe.exe). Writes d3dprobe-<date>-<time>.txt.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3d9.h>

static FILE* g_out;
static int g_software_adapter;  // set in section_dxgi: adapter 0 has DXGI_ADAPTER_FLAG_SOFTWARE (WARP / Basic Render)
static void P(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_out) { vfprintf(g_out, fmt, ap); fflush(g_out); }
  va_end(ap);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
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

// ---- d3dcompiler at run time (for the on-device compile+create test) ----
typedef HRESULT(WINAPI* D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                      ID3DBlob**);
static D3DCompile_t pD3DCompile;

static void load_compiler(const wchar_t* dir) {
  wchar_t path[MAX_PATH + 32];
  swprintf(path, MAX_PATH + 32, L"%ls\\d3dcompiler_47.dll", dir);
  HMODULE m = LoadLibraryW(path);
  if (!m) m = LoadLibraryW(L"d3dcompiler_47.dll");
  pD3DCompile = m ? (D3DCompile_t)(void*)GetProcAddress(m, "D3DCompile") : NULL;
  P("d3dcompiler_47.dll: %s\n", pD3DCompile ? "loaded (D3DCompile found)" : "NOT available - the compile test is skipped");
}

// a pixel shader with n dependent mad ops (SM2 syntax) ~ n arithmetic slots
static int make_ps(char* buf, int cap, int n, int sm4) {
  int o = 0;
  if (sm4)
    o += snprintf(buf + o, cap - o,
                  "Texture2D t:register(t0);SamplerState s:register(s0);\n"
                  "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{float4 c=t.Sample(s,uv);\n");
  else
    o += snprintf(buf + o, cap - o,
                  "sampler2D s:register(s0);\nfloat4 main(float2 uv:TEXCOORD0):COLOR{float4 c=tex2D(s,uv);\n");
  for (int i = 0; i < n; ++i)
    o += snprintf(buf + o, cap - o, "c=mad(c,c.yzwx,c.wzyx+%d.0%d);\n", i % 9, i % 7);
  o += snprintf(buf + o, cap - o, "return c;}\n");
  return o;
}

static int compile_ok(const char* src, int len, const char* profile, ID3DBlob** out) {
  if (!pD3DCompile) return 0;
  ID3DBlob* err = NULL;
  *out = NULL;
  HRESULT hr = pD3DCompile(src, len, "s", NULL, NULL, "main", profile, 0, 0, out, &err);
  if (err) ID3D10Blob_Release(err);
  return SUCCEEDED(hr) && *out;
}

// ---------------------------------------------------------------------------------------------------------------------
static void section_dxgi(void) {
  P("\n== DXGI adapter ==\n");
  HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
  typedef HRESULT(WINAPI * CreateDXGIFactory1_t)(REFIID, void**);
  CreateDXGIFactory1_t pCreate = dxgi ? (CreateDXGIFactory1_t)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
  if (!pCreate) { P("CreateDXGIFactory1 not available\n"); return; }
  IDXGIFactory1* f = NULL;
  if (FAILED(pCreate(&IID_IDXGIFactory1, (void**)&f)) || !f) { P("CreateDXGIFactory1 failed\n"); return; }
  IDXGIAdapter1* a1 = NULL;
  for (UINT i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a1) == S_OK; ++i) {
    IDXGIAdapter2* a2 = NULL;
    if (SUCCEEDED(IDXGIAdapter1_QueryInterface(a1, &IID_IDXGIAdapter2, (void**)&a2)) && a2) {
      DXGI_ADAPTER_DESC2 d;
      if (SUCCEEDED(IDXGIAdapter2_GetDesc2(a2, &d))) {
        P("  adapter %u: %ls\n    vendor 0x%04X device 0x%04X subsys 0x%08X rev %u; flags 0x%X%s\n"
          "    dedicatedVideo %llu MB, dedicatedSystem %llu MB, sharedSystem %llu MB\n",
          i, d.Description, d.VendorId, d.DeviceId, d.SubSysId, d.Revision, (unsigned)d.Flags,
          (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (SOFTWARE)" : "",
          (unsigned long long)(d.DedicatedVideoMemory >> 20), (unsigned long long)(d.DedicatedSystemMemory >> 20),
          (unsigned long long)(d.SharedSystemMemory >> 20));
        if (i == 0 && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) g_software_adapter = 1;
      }
      IDXGIAdapter2_Release(a2);
    } else {
      DXGI_ADAPTER_DESC1 d;
      if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a1, &d)))
        P("  adapter %u (no IDXGIAdapter2): %ls vendor 0x%04X device 0x%04X flags 0x%X\n", i, d.Description, d.VendorId,
          d.DeviceId, (unsigned)d.Flags);
    }
    IDXGIAdapter1_Release(a1);
  }
  IDXGIFactory1_Release(f);
  if (dxgi) FreeLibrary(dxgi);
}

typedef HRESULT(WINAPI* D3D11CreateDevice_t)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT,
                                             UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
static D3D11CreateDevice_t pD3D11CreateDevice;

static void fmt_support(ID3D11Device* dev, const char* name, DXGI_FORMAT fmt) {
  UINT s = 0;
  if (FAILED(ID3D11Device_CheckFormatSupport(dev, fmt, &s))) { P("    %-10s query failed\n", name); return; }
  P("    %-10s%s%s%s%s%s%s%s\n", name,
    (s & D3D11_FORMAT_SUPPORT_TEXTURE2D) ? " tex2d" : "", (s & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ? " rt" : "",
    (s & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ? " sample" : "", (s & D3D11_FORMAT_SUPPORT_DISPLAY) ? " display" : "",
    (s & D3D11_FORMAT_SUPPORT_DECODER_OUTPUT) ? " decoder_out" : "",
    (s & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT) ? " vp_in" : "",
    (s & D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT) ? " vp_out" : "");
}

static void section_d3d11(const char* src330_sm4, int len330_sm4, ID3D11Device** out_dev, D3D_FEATURE_LEVEL* out_fl) {
  P("\n== D3D11 ==\n");
  HMODULE d11 = LoadLibraryW(L"d3d11.dll");
  pD3D11CreateDevice = d11 ? (D3D11CreateDevice_t)(void*)GetProcAddress(d11, "D3D11CreateDevice") : NULL;
  if (!pD3D11CreateDevice) { P("D3D11CreateDevice not available\n"); return; }
  static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                             D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
                                             D3D_FEATURE_LEVEL_9_1};
  P("  D3D11CreateDevice(HARDWARE) per single level:\n");
  for (int i = 0; i < 7; ++i) {
    ID3D11Device* d = NULL;
    D3D_FEATURE_LEVEL got = 0;
    HRESULT hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &levels[i], 1, D3D11_SDK_VERSION, &d, &got,
                                    NULL);
    P("    %-4s -> 0x%08lX%s\n", fl_name(levels[i]), (unsigned long)hr, SUCCEEDED(hr) ? " OK" : "");
    if (d) ID3D11Device_Release(d);
  }
  ID3D11Device* dev = NULL;
  D3D_FEATURE_LEVEL got = 0;
  ID3D11DeviceContext* ctx = NULL;
  HRESULT hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 7, D3D11_SDK_VERSION, &dev, &got, &ctx);
  if (FAILED(hr) || !dev) {
    P("  full descending list: FAILED 0x%08lX (no hardware D3D11 device)\n", (unsigned long)hr);
    if (d11) FreeLibrary(d11);
    return;
  }
  P("  full descending list -> feature level %s\n", fl_name(got));
  *out_dev = dev;
  *out_fl = got;
  D3D11_FEATURE_DATA_THREADING th;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_THREADING, &th, sizeof th)))
    P("  threading: concurrent creates %d, command lists %d\n", th.DriverConcurrentCreates, th.DriverCommandLists);
  D3D11_FEATURE_DATA_D3D9_OPTIONS o9;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D9_OPTIONS, &o9, sizeof o9)))
    P("  D3D9_OPTIONS: full non-pow2 %d\n", o9.FullNonPow2TextureSupport);
  D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT sh;
  if (SUCCEEDED(ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D9_SHADOW_SUPPORT, &sh, sizeof sh)))
    P("  D3D9_SHADOW_SUPPORT: %d\n", sh.SupportsDepthAsTextureWithLessEqualComparisonFilter);
  P("  format support:\n");
  fmt_support(dev, "BGRA8", DXGI_FORMAT_B8G8R8A8_UNORM);
  fmt_support(dev, "RGBA8", DXGI_FORMAT_R8G8B8A8_UNORM);
  fmt_support(dev, "R8", DXGI_FORMAT_R8_UNORM);
  fmt_support(dev, "R8G8", DXGI_FORMAT_R8G8_UNORM);
  fmt_support(dev, "R16F", DXGI_FORMAT_R16_FLOAT);
  fmt_support(dev, "NV12", DXGI_FORMAT_NV12);
  // on-device compile + CreatePixelShader for ps_4_0_level_9_3 (where the level allows it)
  if (pD3DCompile && got >= D3D_FEATURE_LEVEL_9_3) {
    ID3DBlob* blob = NULL;
    int ok = compile_ok(src330_sm4, len330_sm4, "ps_4_0_level_9_3", &blob);
    P("  330-slot ps_4_0_level_9_3: compile %s", ok ? "OK" : "rejected");
    if (ok && blob) {
      ID3D11PixelShader* psh = NULL;
      HRESULT ch = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(blob),
                                                  ID3D10Blob_GetBufferSize(blob), NULL, &psh);
      P("; CreatePixelShader 0x%08lX%s\n", (unsigned long)ch, SUCCEEDED(ch) ? " (driver accepts it)" : "");
      if (psh) ID3D11PixelShader_Release(psh);
    } else {
      P("\n");
    }
    if (blob) ID3D10Blob_Release(blob);
  }
  if (ctx) ID3D11DeviceContext_Release(ctx);
  if (d11) FreeLibrary(d11);
}

static void section_video(ID3D11Device* dev) {
  P("\n== D3D11 video (decode) ==\n");
  if (!dev) { P("  no D3D11 device\n"); return; }
  if (g_software_adapter) {  // server WARP/Basic Render: no real video device, and its video COM calls fault. The device's
    P("  software adapter (server WARP/Basic Render) - no hardware video device; skipping (the device's adapter runs this)\n");
    return;                  // hardware adapter is not SOFTWARE-flagged, so this runs fully on the Surface 2.
  }
  ID3D11VideoDevice* vd = NULL;
  if (FAILED(ID3D11Device_QueryInterface(dev, &IID_ID3D11VideoDevice, (void**)&vd)) || !vd) {
    P("  ID3D11VideoDevice not available\n");
    return;
  }
  UINT n = ID3D11VideoDevice_GetVideoDecoderProfileCount(vd);
  P("  %u decoder profiles:\n", n);
  // H.264 VLD NoFGT = {1b81be68-a0c7-11d3-b984-00c04f2e73c5}
  GUID h264 = {0x1b81be68, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};
  for (UINT i = 0; i < n; ++i) {
    GUID g;
    if (SUCCEEDED(ID3D11VideoDevice_GetVideoDecoderProfile(vd, i, &g)))
      P("    {%08lX-%04X-%04X-...}%s\n", g.Data1, g.Data2, g.Data3,
        (g.Data1 == h264.Data1 && g.Data2 == h264.Data2) ? "  <- H.264 VLD" : "");
  }
  BOOL nv12 = FALSE;
  if (SUCCEEDED(ID3D11VideoDevice_CheckVideoDecoderFormat(vd, &h264, DXGI_FORMAT_NV12, &nv12)))
    P("  H.264 VLD NV12 supported: %d\n", nv12);
  D3D11_VIDEO_DECODER_DESC desc = {h264, 1280, 720, DXGI_FORMAT_NV12};
  UINT cfg = 0;
  if (SUCCEEDED(ID3D11VideoDevice_GetVideoDecoderConfigCount(vd, &desc, &cfg)))
    P("  H.264 1280x720 config count: %u\n", cfg);
  desc.SampleWidth = 1920; desc.SampleHeight = 1080;
  if (SUCCEEDED(ID3D11VideoDevice_GetVideoDecoderConfigCount(vd, &desc, &cfg)))
    P("  H.264 1920x1080 config count: %u\n", cfg);
  ID3D11VideoDevice_Release(vd);
}

static void section_d3d9(const char* src330, int len330, const char* src330_sm4, int len330_sm4) {
  P("\n== D3D9 (Direct3DCreate9Ex) ==\n");
  HMODULE d9 = LoadLibraryW(L"d3d9.dll");
  typedef HRESULT(WINAPI * Create9Ex_t)(UINT, IDirect3D9Ex**);
  Create9Ex_t pCreate = d9 ? (Create9Ex_t)(void*)GetProcAddress(d9, "Direct3DCreate9Ex") : NULL;
  if (!pCreate) { P("  Direct3DCreate9Ex not available\n"); if (d9) FreeLibrary(d9); return; }
  IDirect3D9Ex* d3d = NULL;
  if (FAILED(pCreate(D3D_SDK_VERSION, &d3d)) || !d3d) { P("  Direct3DCreate9Ex failed\n"); if (d9) FreeLibrary(d9); return; }
  D3DCAPS9 caps;
  if (SUCCEEDED(IDirect3D9Ex_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps))) {
    P("  PixelShaderVersion %u.%u  VertexShaderVersion %u.%u\n", D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),
      D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion), D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),
      D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion));
    P("  PS20Caps: Caps 0x%X DynamicFlowControlDepth %d NumTemps %d StaticFlowControlDepth %d NumInstructionSlots %d\n",
      caps.PS20Caps.Caps, caps.PS20Caps.DynamicFlowControlDepth, caps.PS20Caps.NumTemps,
      caps.PS20Caps.StaticFlowControlDepth, caps.PS20Caps.NumInstructionSlots);
    P("  VS20Caps: Caps 0x%X DynamicFlowControlDepth %d NumTemps %d StaticFlowControlDepth %d\n",
      caps.VS20Caps.Caps, caps.VS20Caps.DynamicFlowControlDepth, caps.VS20Caps.NumTemps,
      caps.VS20Caps.StaticFlowControlDepth);
    P("  MaxPixelShader30InstructionSlots %u  MaxTextureWidth %u  SimultaneousRTs %u  MaxVertexShaderConst %u\n",
      caps.MaxPixelShader30InstructionSlots, (unsigned)caps.MaxTextureWidth, caps.NumSimultaneousRTs,
      caps.MaxVertexShaderConst);
    // ps_2_a requires arbitrary swizzle + gradient + predication + >= given slots; ps_2_b just more slots. Report the deciding bits.
    P("  ps_2_x bits: ARBITRARYSWIZZLE %d GRADIENTINSTRUCTIONS %d PREDICATION %d NODEPENDENTREADLIMIT %d NOTEXINSTRUCTIONLIMIT %d\n",
      !!(caps.PS20Caps.Caps & D3DPS20CAPS_ARBITRARYSWIZZLE), !!(caps.PS20Caps.Caps & D3DPS20CAPS_GRADIENTINSTRUCTIONS),
      !!(caps.PS20Caps.Caps & D3DPS20CAPS_PREDICATION), !!(caps.PS20Caps.Caps & D3DPS20CAPS_NODEPENDENTREADLIMIT),
      !!(caps.PS20Caps.Caps & D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT));
  } else {
    P("  GetDeviceCaps(HAL) failed\n");
  }
  // on-device compile + CreatePixelShader on a real D3D9 device for ps_2_0 / ps_2_a / ps_2_b
  HWND hwnd = GetDesktopWindow();
  D3DPRESENT_PARAMETERS pp;
  ZeroMemory(&pp, sizeof pp);
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.BackBufferWidth = 16;
  pp.BackBufferHeight = 16;
  IDirect3DDevice9Ex* dev = NULL;
  HRESULT hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                           D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, NULL, &dev);
  if (FAILED(hr) || !dev) {
    P("  CreateDeviceEx(HAL) 0x%08lX; trying software vertex\n", (unsigned long)hr);
    hr = IDirect3D9Ex_CreateDeviceEx(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, NULL, &dev);
  }
  if (SUCCEEDED(hr) && dev && pD3DCompile) {
    const char* profs[] = {"ps_2_0", "ps_2_a", "ps_2_b"};
    for (int i = 0; i < 3; ++i) {
      ID3DBlob* blob = NULL;
      int ok = compile_ok(src330, len330, profs[i], &blob);
      P("  330-slot %s: compile %s", profs[i], ok ? "OK" : "rejected");
      if (ok && blob) {
        IDirect3DPixelShader9* ps = NULL;
        HRESULT ch = IDirect3DDevice9Ex_CreatePixelShader(dev, (const DWORD*)ID3D10Blob_GetBufferPointer(blob), &ps);
        P("; CreatePixelShader 0x%08lX%s\n", (unsigned long)ch, SUCCEEDED(ch) ? " (driver accepts it)" : "");
        if (ps) IDirect3DPixelShader9_Release(ps);
      } else {
        P("\n");
      }
      if (blob) ID3D10Blob_Release(blob);
    }
  } else if (!dev) {
    P("  no D3D9 device for the compile+create test (hr 0x%08lX)\n", (unsigned long)hr);
  }
  (void)src330_sm4; (void)len330_sm4;
  if (dev) IDirect3DDevice9Ex_Release(dev);
  IDirect3D9Ex_Release(d3d);
  if (d9) FreeLibrary(d9);
}

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  wchar_t dir[MAX_PATH];
  if (argc > 1) { wcsncpy(dir, argv[1], MAX_PATH - 1); dir[MAX_PATH - 1] = 0; }
  else { GetModuleFileNameW(NULL, dir, MAX_PATH); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t path[MAX_PATH + 64];
  swprintf(path, MAX_PATH + 64, L"%ls\\d3dprobe-%04u%02u%02u-%02u%02u%02u.txt", dir, t.wYear, t.wMonth, t.wDay, t.wHour,
           t.wMinute, t.wSecond);
  g_out = _wfopen(path, L"w");
  typedef LONG(WINAPI * RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW v;
  ZeroMemory(&v, sizeof v);
  v.dwOSVersionInfoSize = sizeof v;
  RtlGetVersion_t rgv = (RtlGetVersion_t)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
  if (rgv) rgv(&v);
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  P("d3dprobe 1 (Venetium BATCH-GPU-1 section 2.5, 2026-10-05)\n");
  P("windows %lu.%lu.%lu  native arch %s\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
    si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM ? "ARM32"
    : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64"
    : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" : "?");
  load_compiler(dir);
  // ~330 arithmetic slots: section 2.4 measured ~1.9 slots per dependent vec4 mad op, so 170 ops ~ 330 slots - above ps_2_0's
  // 64 but inside the 512 ceiling of ps_2_a / ps_2_b / ps_4_0_level_9_3. This asks the driver (not just the compiler) to accept
  // a program the size of the device's failing Skia ones.
  static char src330[65536], src330s[65536];
  int len330 = make_ps(src330, sizeof src330, 170, 0);
  int len330s = make_ps(src330s, sizeof src330s, 170, 1);
  ID3D11Device* dev = NULL;
  D3D_FEATURE_LEVEL fl = 0;
#define TRY(stmt) __try { stmt; } __except (EXCEPTION_EXECUTE_HANDLER) { P("  [faulted: 0x%08lX - a software adapter / headless server; the device's real adapter runs this]\n", GetExceptionCode()); }
  TRY(section_dxgi());
  TRY(section_d3d11(src330s, len330s, &dev, &fl));
  TRY(section_video(dev));
  // the D3D11 device is intentionally not Released here: on the server's WARP it fast-fails on teardown (uncatchable); the OS
  // reclaims it at process exit. On the device the driver is fine either way.
  TRY(section_d3d9(src330, len330, src330s, len330s));
#undef TRY
  P("\n== done ==\nlog: %ls\n", path);
  if (g_out) fclose(g_out);
  return 0;
}
