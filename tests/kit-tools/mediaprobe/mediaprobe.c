// mediaprobe.c - Venetium device diagnostics: where do the CAMERA (Media Foundation) and MICROPHONE (WASAPI) break on
// this Windows, and is the WinRT MediaCapture fallback present? This decides Venetium's fix path:
//
//   * If MF enumerates + opens + reads a camera frame here (unsandboxed), and WASAPI does the same for the mic, then the
//     hardware + OS CAN capture and the browser failure is Venetium's sandbox / process model (the 0043/0044 class of
//     ARM32 bug) -> a small Chromium sandbox/token fix, no new capture backend.
//   * If MF sees NO camera (or cannot open it) but the WinRT factories below are present, the desktop capture stack does
//     not work on this device and Venetium needs a WinRT (Windows.Media.Capture) capture backend -> a large change.
//
// It changes nothing on the machine: it enumerates devices, briefly opens the first camera and the default mic to pull a
// frame / a few audio packets, then stops. No registry writes, no files except its log. A camera/mic privacy prompt may
// appear - that is the OS, and allowing it is the point of the test.
//
// C (rt2.1 clang-cl cannot emit Windows ARM32 C++ exceptions, TOOLCHAIN B4), using the COM headers' C vtable macros
// (COBJMACROS). mfplat/mf/mfreadwrite/mfuuid, ole32 (WASAPI via CoCreateInstance), runtimeobject (WinRT factory probe).
//
// Usage: mediaprobe.exe [output-dir]   (default: the folder holding mediaprobe.exe). Writes mediaprobe-<date>-<time>.txt.
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propidl.h>
#include <roapi.h>
#include <winstring.h>

// ---- GUIDs hardcoded so we need no INITGUID (which would clash with mfuuid.lib's MF guids) and no uuid.lib ambiguity.
static const CLSID kCLSID_MMDeviceEnumerator =
    {0xBCDE0395,0xE52F,0x467C,{0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
static const IID kIID_IMMDeviceEnumerator =
    {0xA95664D2,0x9614,0x4F35,{0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
static const IID kIID_IAudioClient =
    {0x1CB9AD4C,0xDBFA,0x4C32,{0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
static const IID kIID_IAudioCaptureClient =
    {0xC8ADBD64,0xE71E,0x48A0,{0xA4,0xDE,0x18,0x5C,0x39,0x5C,0xD3,0x17}};
static const IID kIID_IMFMediaSource =
    {0x279A808D,0xAEC7,0x40C8,{0x9C,0x6B,0xA6,0xB4,0x92,0xC7,0x8A,0x66}};
static const IID kIID_IActivationFactory =
    {0x00000035,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
// PKEY_Device_FriendlyName
static const PROPERTYKEY kPKEY_FriendlyName =
    {{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}}, 14};

static FILE* g_out;

static void P(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_out) { vfprintf(g_out, fmt, ap); fflush(g_out); }
  va_end(ap);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static const char* HR(HRESULT hr) {
  static char b[16];
  snprintf(b, sizeof b, "0x%08lX", (unsigned long)hr);
  return b;
}

// wide -> UTF-8 into a rotating set of static buffers (fine for one-at-a-time logging)
static const char* U8(const wchar_t* w) {
  static char bufs[4][512];
  static int idx;
  char* b = bufs[idx++ & 3];
  if (!w) { b[0] = 0; return b; }
  WideCharToMultiByte(CP_UTF8, 0, w, -1, b, 512, NULL, NULL);
  return b;
}

typedef LONG(WINAPI* PFN_RtlGetVersion)(PRTL_OSVERSIONINFOW);
static void print_os(void) {
  RTL_OSVERSIONINFOW v;
  HMODULE nt = GetModuleHandleW(L"ntdll.dll");
  PFN_RtlGetVersion p = nt ? (PFN_RtlGetVersion)(void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
  v.dwOSVersionInfoSize = sizeof v;
  if (p && p(&v) == 0)
    P("windows %lu.%lu.%lu  ", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
#if defined(_M_ARM)
  P("native arch ARM32\n");
#elif defined(_M_IX86)
  P("native arch x86\n");
#elif defined(_M_X64)
  P("native arch x64\n");
#else
  P("\n");
#endif
}

// Read up to one video frame from the reader's current media type; 1 on success. Mirrors what the browser does after
// it has chosen a media type (the bare-reader ReadSample a probe would do first fails on cameras that expose a hardware
// MFT with MF_E_HW_MFT_FAILED_START_STREAMING = 0xC00D3704, which says nothing about whether capture really works).
static int try_read(IMFSourceReader* reader, const char* label) {
  for (int t = 0; t < 25; t++) {
    DWORD streamIdx = 0, flags = 0;
    LONGLONG ts = 0;
    IMFSample* samp = NULL;
    HRESULT hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                            &streamIdx, &flags, &ts, &samp);
    if (FAILED(hr)) { P("  ReadSample [%s]: %s\n", label, HR(hr)); return 0; }
    if (samp) {
      DWORD len = 0;
      IMFSample_GetTotalLength(samp, &len);
      P("  ReadSample [%s]: GOT A FRAME, %lu bytes -> CAMERA CAPTURE WORKS\n", label, (unsigned long)len);
      IMFSample_Release(samp);
      return 1;
    }
    Sleep(80);  // no data yet (stream tick) - wait for the first frame
  }
  P("  ReadSample [%s]: opened but no frame within ~2s\n", label);
  return 0;
}

// ============================ Media Foundation video capture ============================
static void probe_mf_video(void) {
  P("\n== camera: Media Foundation ==\n");
  HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  P("  MFStartup: %s\n", HR(hr));
  if (FAILED(hr)) { P("  -> Media Foundation is not available on this Windows\n"); return; }

  IMFAttributes* attr = NULL;
  hr = MFCreateAttributes(&attr, 1);
  if (SUCCEEDED(hr))
    hr = IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                               &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) { P("  MFCreateAttributes/SetGUID: %s\n", HR(hr)); MFShutdown(); return; }

  IMFActivate** devices = NULL;
  UINT32 count = 0;
  hr = MFEnumDeviceSources(attr, &devices, &count);
  IMFAttributes_Release(attr);
  P("  MFEnumDeviceSources(VIDCAP): %s  count=%u\n", HR(hr), count);
  if (FAILED(hr)) { MFShutdown(); return; }

  for (UINT32 i = 0; i < count; i++) {
    WCHAR* name = NULL;
    UINT32 nlen = 0;
    if (SUCCEEDED(IMFActivate_GetAllocatedString(devices[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nlen))) {
      P("    cam[%u]: %s\n", i, U8(name));
      CoTaskMemFree(name);
    }
  }
  if (count == 0) {
    P("  -> MediaFoundation sees NO camera (enumeration empty)\n");
  } else {
    IMFMediaSource* src = NULL;
    hr = IMFActivate_ActivateObject(devices[0], &kIID_IMFMediaSource, (void**)&src);
    P("  ActivateObject(cam[0]): %s\n", HR(hr));
    if (SUCCEEDED(hr)) {
      IMFSourceReader* reader = NULL;
      hr = MFCreateSourceReaderFromMediaSource(src, NULL, &reader);
      P("  MFCreateSourceReaderFromMediaSource: %s\n", HR(hr));
      if (SUCCEEDED(hr)) {
        // list the camera's native formats (what it actually offers)
        for (DWORD ti = 0; ti < 64; ti++) {
          IMFMediaType* mt = NULL;
          if (FAILED(IMFSourceReader_GetNativeMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, ti, &mt)))
            break;
          GUID sub;
          UINT64 fs = 0, fr = 0;
          memset(&sub, 0, sizeof sub);
          IMFMediaType_GetGUID(mt, &MF_MT_SUBTYPE, &sub);
          IMFMediaType_GetUINT64(mt, &MF_MT_FRAME_SIZE, &fs);
          IMFMediaType_GetUINT64(mt, &MF_MT_FRAME_RATE, &fr);
          unsigned char* fc = (unsigned char*)&sub.Data1;
          char nm[16];
          if (fc[0] >= 32 && fc[0] < 127 && fc[1] >= 32 && fc[1] < 127 && fc[2] >= 32 && fc[2] < 127 && fc[3] >= 32 && fc[3] < 127)
            snprintf(nm, sizeof nm, "%c%c%c%c", fc[0], fc[1], fc[2], fc[3]);
          else
            snprintf(nm, sizeof nm, "%08lX", (unsigned long)sub.Data1);
          ULONG frn = (ULONG)(fr >> 32), frd = (ULONG)(fr & 0xffffffff);
          P("    native[%lu]: %s %lux%lu @ %.1f fps\n", (unsigned long)ti, nm,
            (unsigned long)(fs >> 32), (unsigned long)(fs & 0xffffffff), frd ? (double)frn / (double)frd : 0.0);
          IMFMediaType_Release(mt);
        }
        int got = 0;
        // A) the camera's own first native type
        IMFMediaType* n0 = NULL;
        if (SUCCEEDED(IMFSourceReader_GetNativeMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &n0))) {
          HRESULT sh = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, n0);
          P("  SetCurrentMediaType(native[0]): %s\n", HR(sh));
          if (SUCCEEDED(sh)) got = try_read(reader, "native[0]");
          IMFMediaType_Release(n0);
        }
        // B) ask the reader to decode to NV12 (what the browser usually requests)
        if (!got) {
          IMFMediaType* want = NULL;
          if (SUCCEEDED(MFCreateMediaType(&want))) {
            IMFMediaType_SetGUID(want, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            IMFMediaType_SetGUID(want, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
            HRESULT sh = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, want);
            P("  SetCurrentMediaType(NV12): %s\n", HR(sh));
            if (SUCCEEDED(sh)) got = try_read(reader, "NV12");
            IMFMediaType_Release(want);
          }
        }
        // C) or RGB32
        if (!got) {
          IMFMediaType* want = NULL;
          if (SUCCEEDED(MFCreateMediaType(&want))) {
            IMFMediaType_SetGUID(want, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
            IMFMediaType_SetGUID(want, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
            HRESULT sh = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, want);
            P("  SetCurrentMediaType(RGB32): %s\n", HR(sh));
            if (SUCCEEDED(sh)) got = try_read(reader, "RGB32");
            IMFMediaType_Release(want);
          }
        }
        if (!got) P("  -> camera opens but will not stream in any tried format (codes above)\n");
        IMFSourceReader_Release(reader);
      }
      IMFMediaSource_Shutdown(src);
      IMFMediaSource_Release(src);
    } else {
      P("  -> MediaFoundation enumerated the camera but could not open it\n");
    }
  }
  for (UINT32 i = 0; i < count; i++) IMFActivate_Release(devices[i]);
  if (devices) CoTaskMemFree(devices);
  MFShutdown();
}

// ============================ WASAPI microphone ============================
static void probe_wasapi_mic(void) {
  P("\n== microphone: WASAPI (Core Audio) ==\n");
  IMMDeviceEnumerator* en = NULL;
  HRESULT hr = CoCreateInstance(&kCLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &kIID_IMMDeviceEnumerator, (void**)&en);
  P("  CoCreateInstance(MMDeviceEnumerator): %s\n", HR(hr));
  if (FAILED(hr)) return;

  IMMDeviceCollection* coll = NULL;
  UINT cnt = 0;
  hr = IMMDeviceEnumerator_EnumAudioEndpoints(en, eCapture, DEVICE_STATE_ACTIVE, &coll);
  if (SUCCEEDED(hr)) IMMDeviceCollection_GetCount(coll, &cnt);
  P("  EnumAudioEndpoints(eCapture, ACTIVE): %s  count=%u\n", HR(hr), cnt);
  if (SUCCEEDED(hr)) {
    for (UINT i = 0; i < cnt; i++) {
      IMMDevice* d = NULL;
      if (FAILED(IMMDeviceCollection_Item(coll, i, &d))) continue;
      IPropertyStore* ps = NULL;
      if (SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &ps))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(IPropertyStore_GetValue(ps, &kPKEY_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
          P("    mic[%u]: %s\n", i, U8(pv.pwszVal));
        PropVariantClear(&pv);
        IPropertyStore_Release(ps);
      }
      IMMDevice_Release(d);
    }
    IMMDeviceCollection_Release(coll);
  }
  if (cnt == 0) P("  -> WASAPI sees NO active capture endpoint\n");

  IMMDevice* def = NULL;
  hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eCapture, eConsole, &def);
  P("  GetDefaultAudioEndpoint(eCapture): %s\n", HR(hr));
  if (SUCCEEDED(hr)) {
    IAudioClient* ac = NULL;
    hr = IMMDevice_Activate(def, &kIID_IAudioClient, CLSCTX_ALL, NULL, (void**)&ac);
    P("  Activate(IAudioClient): %s\n", HR(hr));
    if (SUCCEEDED(hr)) {
      WAVEFORMATEX* wf = NULL;
      hr = IAudioClient_GetMixFormat(ac, &wf);
      P("  GetMixFormat: %s\n", HR(hr));
      if (SUCCEEDED(hr)) {
        P("    format: %u ch, %lu Hz, %u-bit\n", wf->nChannels, (unsigned long)wf->nSamplesPerSec, wf->wBitsPerSample);
        hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, 0, 2000000, 0, wf, NULL);
        P("  IAudioClient::Initialize(shared): %s\n", HR(hr));
        if (SUCCEEDED(hr)) {
          IAudioCaptureClient* cap = NULL;
          hr = IAudioClient_GetService(ac, &kIID_IAudioCaptureClient, (void**)&cap);
          P("  GetService(IAudioCaptureClient): %s\n", HR(hr));
          if (SUCCEEDED(hr)) {
            hr = IAudioClient_Start(ac);
            P("  IAudioClient::Start: %s\n", HR(hr));
            if (SUCCEEDED(hr)) {
              UINT32 total = 0;
              for (int t = 0; t < 20; t++) {
                UINT32 pkt = 0;
                IAudioCaptureClient_GetNextPacketSize(cap, &pkt);
                while (pkt) {
                  BYTE* data = NULL;
                  UINT32 frames = 0;
                  DWORD fl = 0;
                  if (SUCCEEDED(IAudioCaptureClient_GetBuffer(cap, &data, &frames, &fl, NULL, NULL))) {
                    total += frames;
                    IAudioCaptureClient_ReleaseBuffer(cap, frames);
                  } else {
                    break;
                  }
                  IAudioCaptureClient_GetNextPacketSize(cap, &pkt);
                }
                Sleep(50);
              }
              P("  captured %u mic frames -> %s\n", total, total ? "MICROPHONE CAPTURE WORKS" : "opened but NO audio frames");
              IAudioClient_Stop(ac);
            }
            IAudioCaptureClient_Release(cap);
          }
        }
        CoTaskMemFree(wf);
      }
      IAudioClient_Release(ac);
    }
    IMMDevice_Release(def);
  }
  IMMDeviceEnumerator_Release(en);
}

// ============================ WinRT availability (fallback target) ============================
static void probe_winrt(void) {
  P("\n== WinRT availability (the fallback capture path) ==\n");
  HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
  int inited = SUCCEEDED(hr) || hr == S_FALSE;
  int changed_mode = (hr == RPC_E_CHANGED_MODE);
  P("  RoInitialize: %s\n", HR(hr));
  static const wchar_t* classes[] = {
    L"Windows.Media.Capture.MediaCapture",
    L"Windows.Devices.Enumeration.DeviceInformation",
    L"Windows.Media.Capture.Frames.MediaFrameSourceGroup",
  };
  for (int i = 0; i < 3; i++) {
    HSTRING h = NULL;
    WindowsCreateString(classes[i], (UINT32)wcslen(classes[i]), &h);
    IActivationFactory* f = NULL;
    HRESULT g = RoGetActivationFactory(h, &kIID_IActivationFactory, (void**)&f);
    P("  factory %s : %s\n", U8(classes[i]), HR(g));
    if (f) IUnknown_Release((IUnknown*)f);
    if (h) WindowsDeleteString(h);
  }
  if (inited && !changed_mode) RoUninitialize();
}

int main(int argc, char** argv) {
  wchar_t dir[MAX_PATH];
  if (argc >= 2) {
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dir, MAX_PATH);
  } else {
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    wchar_t* s = wcsrchr(dir, L'\\');
    if (s) *s = 0;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t path[MAX_PATH + 64];
  swprintf(path, MAX_PATH + 64, L"%ls\\mediaprobe-%04d%02d%02d-%02d%02d%02d.txt",
           dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  g_out = _wfopen(path, L"wb");
  if (g_out) {
    static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, 1, 3, g_out);
  }

  CoInitializeEx(NULL, COINIT_MULTITHREADED);
  P("mediaprobe 2 (Venetium) - camera + microphone capability probe\n");
  print_os();
  probe_mf_video();
  probe_wasapi_mic();
  probe_winrt();
  P("\n== done ==\n");
  {
    char p8[MAX_PATH * 2];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, p8, sizeof p8, NULL, NULL);
    P("log: %s\n", p8);
  }
  if (g_out) fclose(g_out);
  CoUninitialize();
  return 0;
}
