// winrtcamprobe.cpp v4 - Venetium device probe: which WinRT camera primitive actually STREAMS on the NVIDIA AvStream
// camera, and at what frame rate? Photo capture (CapturePhotoToStreamAsync / LowLagPhotoCapture) works but is capped at
// ~2 fps on this camera; desktop Media Foundation can't start-stream it at all (0xC00D3704). v4 measures the streaming
// candidates, every one initialised like the proven photo path (TelegArm recipe: StreamingCaptureMode=AudioAndVideo with
// a Video fallback, PhotoCaptureSource=Auto, and NO MemoryPreference - forcing Cpu breaks this camera):
//   A  MediaFrameReader (fr-*): VideoPreview and VideoRecord sources, default and <=640x480 formats, NV12 output, a
//      MediaFrameSourceGroup-initialised variant, and a record-source reader while a preview runs. Logs FrameArrived
//      rate, distinct frames (timestamp + content hash), SoftwareBitmap vs Direct3DSurface vs BufferMediaFrame, pixel
//      format, plane layout, lock+copy cost, and a luma check (live image vs black frames).
//   B  StartPreviewAsync + GetPreviewFrameAsync polling (prev-*), native and Bgra8-copy, default and 640x480 preview.
//   R  StartRecordToStreamAsync(MP4 640x480, video only) for a few seconds; parses the MP4 for the recorded frame rate
//      (tells whether a custom-sink backend, plan C, would stream).
//   P  CapturePhotoToStreamAsync x5 - the current backend's rate: first as a camera health check, last as a canary.
// Every streaming test waits up to 10 s for its first frame, then measures a fixed window from that frame; the verdict
// uses SUSTAINED fps (distinct frames / window), so a burst-then-stall or a re-served cached frame can't read as
// STREAMS. Each test runs in its own child process (fresh camera state; a hang or crash only loses that test - per-test
// watchdog + crash filter), all appending to one log that ends with a SUMMARY + WINNER line. Results are logged BEFORE
// the camera is torn down, so a teardown hang can't lose a measurement.
//
// Usage: winrtcamprobe.exe                      run everything (camera 0 full suite, camera 1 short suite), then wait for Enter
//        winrtcamprobe.exe --tests fr-prev,prev --cam 1 --secs 6 --nopause
//        winrtcamprobe.exe <dir>                (old form) write the log into <dir>
// C++ + WRL (HRESULT-based COM, NO C++ exceptions - matches Chromium and this ARM32 toolchain; .c name kept so the kit
// build globs it, compiled as C++ by build.sh). Changes nothing on the system; a camera-consent prompt may appear.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.devices.enumeration.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.imaging.h>
#include <windows.media.h>
#include <windows.media.capture.h>
#include <windows.media.capture.frames.h>
#include <windows.media.devices.h>
#include <windows.media.mediaproperties.h>
#include <windows.storage.streams.h>
#include <MemoryBuffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
namespace wf = ABI::Windows::Foundation;
namespace wfc = ABI::Windows::Foundation::Collections;
namespace wde = ABI::Windows::Devices::Enumeration;
namespace wgd = ABI::Windows::Graphics::DirectX::Direct3D11;
namespace wgi = ABI::Windows::Graphics::Imaging;
namespace wm = ABI::Windows::Media;
namespace wmc = ABI::Windows::Media::Capture;
namespace wmcf = ABI::Windows::Media::Capture::Frames;
namespace wmd = ABI::Windows::Media::Devices;
namespace wmp = ABI::Windows::Media::MediaProperties;
namespace wss = ABI::Windows::Storage::Streams;

#define PROBE_VERSION "4"
#define FIRST_FRAME_WAIT_MS 10000.0  // how long a streaming test waits for its first frame before NO-FRAMES

// ---------------------------------------------------------------------------------------------------------------------
// logging (thread-safe: WinRT callbacks log from pool threads). The file is written under the lock; the console
// outside it, so a blocked console (QuickEdit selection) can't freeze the file log or the watchdog.

static FILE* g_out;
static CRITICAL_SECTION g_logcs;
static LARGE_INTEGER g_qpf;
static HMODULE g_mferr;  // mferror.dll as a resource module: Media Foundation error text
static char g_test[32] = "main";
static int g_cam;
static const char* volatile g_step = "start";
static double g_t0;  // test start (ms)
#define STEP(s) (g_step = (s))

static double NowMs() {
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart * 1000.0 / (double)g_qpf.QuadPart;
}

static void LogWrite(const char* s, bool console) {
  EnterCriticalSection(&g_logcs);
  if (g_out) { fputs(s, g_out); fflush(g_out); }
  LeaveCriticalSection(&g_logcs);
  if (console) { fputs(s, stdout); fflush(stdout); }
}

static void P(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  LogWrite(buf, true);
}

static void CloseLog() {
  EnterCriticalSection(&g_logcs);
  FILE* f = g_out;
  g_out = NULL;
  LeaveCriticalSection(&g_logcs);
  if (f) fclose(f);
}

// Rotating scratch strings so helpers can return char* from any thread.
static char* Slot() {
  static char slots[32][600];
  static volatile LONG idx;
  return slots[(unsigned)InterlockedIncrement(&idx) & 31];
}

static const char* HR(HRESULT hr) {
  char* b = Slot();
  int n = snprintf(b, 600, "0x%08lX", (unsigned long)hr);
  if (FAILED(hr) && n > 0) {
    char msg[300];
    DWORD m = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)hr, 0, msg,
                             sizeof msg, NULL);
    if (!m && g_mferr)
      m = FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, g_mferr, (DWORD)hr, 0, msg,
                         sizeof msg, NULL);
    if (m) {
      while (m && (msg[m - 1] == '\r' || msg[m - 1] == '\n' || msg[m - 1] == ' ' || msg[m - 1] == '.')) msg[--m] = 0;
      snprintf(b + n, 600 - n, " (%s)", msg);
    }
  }
  return b;
}

static const char* U8(HSTRING h) {
  char* b = Slot();
  b[0] = 0;
  UINT32 n = 0;
  const wchar_t* w = WindowsGetStringRawBuffer(h, &n);
  if (w && n) {
    int k = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, b, 599, NULL, NULL);
    b[k > 0 ? k : 0] = 0;
  }
  return b;
}

static void ResultV(bool console, const char* fmt, va_list ap) {
  char msg[1500], line[1700];
  vsnprintf(msg, sizeof msg, fmt, ap);
  snprintf(line, sizeof line, "RESULT %-12s cam%d: %s\n", g_test, g_cam, msg);
  LogWrite(line, console);
}
static void Result(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  ResultV(true, fmt, ap);
  va_end(ap);
}
static void ResultFileOnly(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  ResultV(false, fmt, ap);
  va_end(ap);
}

// ---------------------------------------------------------------------------------------------------------------------
// blocking WinRT async helpers (MTA: completions arrive on pool threads, so a plain event wait is safe)

static HRESULT AsyncResult(IUnknown* op) {
  ComPtr<wf::IAsyncInfo> info;
  if (FAILED(op->QueryInterface(IID_PPV_ARGS(&info)))) return E_NOINTERFACE;
  wf::AsyncStatus st = wf::AsyncStatus::Started;
  info->get_Status(&st);
  if (st == wf::AsyncStatus::Completed) return S_OK;
  HRESULT ec = S_OK;
  info->get_ErrorCode(&ec);
  if (st == wf::AsyncStatus::Canceled) return FAILED(ec) ? ec : E_ABORT;
  return FAILED(ec) ? ec : E_FAIL;
}

static HRESULT TimedOut(IUnknown* op) {
  ComPtr<wf::IAsyncInfo> info;
  if (SUCCEEDED(op->QueryInterface(IID_PPV_ARGS(&info)))) info->Cancel();
  return HRESULT_FROM_WIN32(ERROR_TIMEOUT);  // the event is leaked on purpose: the handler may still fire later
}

static HRESULT AwaitAction(wf::IAsyncAction* op, DWORD ms = 10000) {
  if (!op) return E_POINTER;
  HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!h) return HRESULT_FROM_WIN32(GetLastError());
  auto cb = Callback<wf::IAsyncActionCompletedHandler>(
      [h](wf::IAsyncAction*, wf::AsyncStatus) -> HRESULT { SetEvent(h); return S_OK; });
  HRESULT hr = op->put_Completed(cb.Get());
  if (FAILED(hr)) { CloseHandle(h); return hr; }
  if (WaitForSingleObject(h, ms) != WAIT_OBJECT_0) return TimedOut(op);
  CloseHandle(h);
  return AsyncResult(op);
}

template <typename T>
static HRESULT AwaitOp(wf::IAsyncOperation<T>* op, DWORD ms = 10000) {
  if (!op) return E_POINTER;
  HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!h) return HRESULT_FROM_WIN32(GetLastError());
  auto cb = Callback<wf::IAsyncOperationCompletedHandler<T>>(
      [h](wf::IAsyncOperation<T>*, wf::AsyncStatus) -> HRESULT { SetEvent(h); return S_OK; });
  HRESULT hr = op->put_Completed(cb.Get());
  if (FAILED(hr)) { CloseHandle(h); return hr; }
  if (WaitForSingleObject(h, ms) != WAIT_OBJECT_0) return TimedOut(op);
  CloseHandle(h);
  return AsyncResult(op);
}

template <typename T>
static void CloseIt(T* p) {
  if (!p) return;
  ComPtr<wf::IClosable> c;
  if (SUCCEEDED(p->QueryInterface(IID_PPV_ARGS(&c))) && c) c->Close();
}

// ---------------------------------------------------------------------------------------------------------------------
// names

static const char* StreamTypeName(wmc::MediaStreamType t) {
  switch (t) {
    case wmc::MediaStreamType_VideoPreview: return "VideoPreview";
    case wmc::MediaStreamType_VideoRecord: return "VideoRecord";
    case wmc::MediaStreamType_Audio: return "Audio";
    case wmc::MediaStreamType_Photo: return "Photo";
    default: { char* b = Slot(); snprintf(b, 600, "type%d", (int)t); return b; }
  }
}
static const char* KindName(wmcf::MediaFrameSourceKind k) {
  switch (k) {
    case wmcf::MediaFrameSourceKind_Custom: return "Custom";
    case wmcf::MediaFrameSourceKind_Color: return "Color";
    case wmcf::MediaFrameSourceKind_Infrared: return "Infrared";
    case wmcf::MediaFrameSourceKind_Depth: return "Depth";
    default: { char* b = Slot(); snprintf(b, 600, "kind%d", (int)k); return b; }
  }
}
static const char* PixName(wgi::BitmapPixelFormat f) {
  switch (f) {
    case wgi::BitmapPixelFormat_Nv12: return "Nv12";
    case wgi::BitmapPixelFormat_Yuy2: return "Yuy2";
    case wgi::BitmapPixelFormat_Bgra8: return "Bgra8";
    case wgi::BitmapPixelFormat_Rgba8: return "Rgba8";
    case wgi::BitmapPixelFormat_Gray8: return "Gray8";
    case wgi::BitmapPixelFormat_Gray16: return "Gray16";
    case wgi::BitmapPixelFormat_Rgba16: return "Rgba16";
    case wgi::BitmapPixelFormat_Unknown: return "Unknown";
    default: { char* b = Slot(); snprintf(b, 600, "pix%d", (int)f); return b; }
  }
}
static const char* CamStateName(wmd::CameraStreamState s) {
  switch (s) {
    case wmd::CameraStreamState_NotStreaming: return "NotStreaming";
    case wmd::CameraStreamState_Streaming: return "Streaming";
    case wmd::CameraStreamState_BlockedForPrivacy: return "BlockedForPrivacy";
    case wmd::CameraStreamState_Shutdown: return "Shutdown";
    default: return "?";
  }
}
static const char* StartStatusName(wmcf::MediaFrameReaderStartStatus s) {
  switch (s) {
    case wmcf::MediaFrameReaderStartStatus_Success: return "Success";
    case wmcf::MediaFrameReaderStartStatus_UnknownFailure: return "UnknownFailure";
    case wmcf::MediaFrameReaderStartStatus_DeviceNotAvailable: return "DeviceNotAvailable";
    case wmcf::MediaFrameReaderStartStatus_OutputFormatNotSupported: return "OutputFormatNotSupported";
    default: return "ExclusiveControlNotAvailable(4)/other";
  }
}
static const char* CharacteristicName(wmc::VideoDeviceCharacteristic c) {
  switch (c) {
    case wmc::VideoDeviceCharacteristic_AllStreamsIndependent: return "AllStreamsIndependent";
    case wmc::VideoDeviceCharacteristic_PreviewRecordStreamsIdentical: return "PreviewRecordStreamsIdentical";
    case wmc::VideoDeviceCharacteristic_PreviewPhotoStreamsIdentical: return "PreviewPhotoStreamsIdentical";
    case wmc::VideoDeviceCharacteristic_RecordPhotoStreamsIdentical: return "RecordPhotoStreamsIdentical";
    case wmc::VideoDeviceCharacteristic_AllStreamsIdentical: return "AllStreamsIdentical";
    default: return "?";
  }
}
static const char* PanelName(int p) {
  static const char* n[] = {"panel?", "Front", "Back", "Top", "Bottom", "Left", "Right"};
  return (p >= 0 && p <= 6) ? n[p] : "panel?";
}
static const char* Verdict(double fps) { return fps >= 10.0 ? "STREAMS" : (fps > 0.0 ? "SLOW" : "NO-FRAMES"); }

static double RatioFps(wmp::IMediaRatio* r) {
  if (!r) return 0.0;
  UINT32 num = 0, den = 0;
  r->get_Numerator(&num);
  r->get_Denominator(&den);
  return den ? (double)num / (double)den : 0.0;
}

static bool SubtypeIs(HSTRING s, const wchar_t* want) {
  UINT32 n = 0;
  const wchar_t* w = WindowsGetStringRawBuffer(s, &n);
  return w && _wcsicmp(w, want) == 0;
}
static bool Uncompressed(HSTRING s) {
  return SubtypeIs(s, L"YUY2") || SubtypeIs(s, L"NV12") || SubtypeIs(s, L"RGB32") || SubtypeIs(s, L"ARGB32") ||
         SubtypeIs(s, L"RGB24") || SubtypeIs(s, L"IYUV") || SubtypeIs(s, L"I420") || SubtypeIs(s, L"BGRA8");
}

// "YUY2 640x480 @30.00"
static const char* DescribeFrameFormat(wmcf::IMediaFrameFormat* f) {
  char* b = Slot();
  if (!f) { snprintf(b, 600, "(none)"); return b; }
  HString sub;
  f->get_Subtype(sub.GetAddressOf());
  UINT32 w = 0, h = 0;
  ComPtr<wmcf::IVideoMediaFrameFormat> vf;
  if (SUCCEEDED(f->get_VideoFormat(&vf)) && vf) { vf->get_Width(&w); vf->get_Height(&h); }
  ComPtr<wmp::IMediaRatio> r;
  f->get_FrameRate(&r);
  snprintf(b, 600, "%s %ux%u @%.2f", U8(sub.Get()), w, h, RatioFps(r.Get()));
  return b;
}

// "Video/YUY2 640x480 @30.00" for IMediaEncodingProperties (video or image)
static const char* DescribeProps(wmp::IMediaEncodingProperties* p, UINT32* wo = nullptr, UINT32* ho = nullptr,
                                 double* fpso = nullptr) {
  char* b = Slot();
  if (!p) { snprintf(b, 600, "(none)"); return b; }
  HString type, sub;
  p->get_Type(type.GetAddressOf());
  p->get_Subtype(sub.GetAddressOf());
  UINT32 w = 0, h = 0;
  double fps = 0;
  ComPtr<wmp::IVideoEncodingProperties> vp;
  ComPtr<wmp::IImageEncodingProperties> ip;
  if (SUCCEEDED(p->QueryInterface(IID_PPV_ARGS(&vp))) && vp) {
    vp->get_Width(&w);
    vp->get_Height(&h);
    ComPtr<wmp::IMediaRatio> r;
    vp->get_FrameRate(&r);
    fps = RatioFps(r.Get());
  } else if (SUCCEEDED(p->QueryInterface(IID_PPV_ARGS(&ip))) && ip) {
    ip->get_Width(&w);
    ip->get_Height(&h);
  }
  if (wo) *wo = w;
  if (ho) *ho = h;
  if (fpso) *fpso = fps;
  snprintf(b, 600, "%s/%s %ux%u @%.2f", U8(type.Get()), U8(sub.Get()), w, h, fps);
  return b;
}

// Format preference for a fast, Chromium-friendly raw stream: fits in 640x480 > uncompressed > >=24 fps > larger
// area > higher fps. (Nothing fits -> the smallest.) Returns true if candidate (w,h,fps,unc) beats the best so far.
struct Pick { bool have; bool fits; bool fast; UINT32 area; double fps; bool unc; };
static bool Better(Pick* best, UINT32 w, UINT32 h, double fps, bool unc) {
  Pick c = {true, w <= 640 && h <= 480, fps >= 24.0, w * h, fps, unc};
  bool win;
  if (!best->have) win = true;
  else if (c.fits != best->fits) win = c.fits;
  else if (c.unc != best->unc) win = c.unc;
  else if (c.fast != best->fast) win = c.fast;
  else if (c.area != best->area) win = c.fits ? (c.area > best->area) : (c.area < best->area);
  else win = c.fps > best->fps;
  if (win) *best = c;
  return win;
}

// ---------------------------------------------------------------------------------------------------------------------
// camera enumeration + MediaCapture init

struct Cam { HString id; char name[256]; int panel; };

static int EnumCams(Cam* cams, int max, bool log) {
  STEP("enumerate cameras");
  ComPtr<wde::IDeviceInformationStatics> st;
  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_Devices_Enumeration_DeviceInformation).Get(), IID_PPV_ARGS(&st));
  if (FAILED(hr)) { P("  DeviceInformation factory: %s\n", HR(hr)); return 0; }
  ComPtr<wf::IAsyncOperation<wde::DeviceInformationCollection*>> op;
  hr = st->FindAllAsyncDeviceClass(wde::DeviceClass_VideoCapture, &op);
  if (SUCCEEDED(hr)) hr = AwaitOp(op.Get());
  ComPtr<wfc::IVectorView<wde::DeviceInformation*>> devs;
  if (SUCCEEDED(hr)) hr = op->GetResults(&devs);
  if (FAILED(hr) || !devs) { P("  FindAllAsync(VideoCapture): %s\n", HR(hr)); return 0; }
  UINT32 n = 0;
  devs->get_Size(&n);
  int k = 0;
  for (UINT32 i = 0; i < n && k < max; i++) {
    ComPtr<wde::IDeviceInformation> di;
    if (FAILED(devs->GetAt(i, &di)) || !di) continue;
    HString name;
    di->get_Name(name.GetAddressOf());
    di->get_Id(cams[k].id.GetAddressOf());
    snprintf(cams[k].name, sizeof cams[k].name, "%s", U8(name.Get()));
    cams[k].panel = 0;
    ComPtr<wde::IEnclosureLocation> loc;
    if (SUCCEEDED(di->get_EnclosureLocation(&loc)) && loc) {
      wde::Panel p = wde::Panel_Unknown;
      loc->get_Panel(&p);
      cams[k].panel = (int)p;
    }
    if (log) P("  cam[%d]: %s [%s]  id=%s\n", k, cams[k].name, PanelName(cams[k].panel), U8(cams[k].id.Get()));
    k++;
  }
  return k;
}

static HRESULT NewSettings(ComPtr<wmc::IMediaCaptureInitializationSettings>& st) {
  ComPtr<IInspectable> insp;
  HRESULT hr = RoActivateInstance(
      HStringReference(RuntimeClass_Windows_Media_Capture_MediaCaptureInitializationSettings).Get(), &insp);
  if (SUCCEEDED(hr)) hr = insp.As(&st);
  return hr;
}

static HRESULT InitWith(wmc::IMediaCaptureInitializationSettings* st, ComPtr<wmc::IMediaCapture>& out) {
  ComPtr<IInspectable> insp;
  HRESULT hr = RoActivateInstance(HStringReference(RuntimeClass_Windows_Media_Capture_MediaCapture).Get(), &insp);
  ComPtr<wmc::IMediaCapture> mc;
  if (SUCCEEDED(hr)) hr = insp.As(&mc);
  if (FAILED(hr)) return hr;
  ComPtr<wf::IAsyncAction> act;
  hr = mc->InitializeWithSettingsAsync(st, &act);
  if (SUCCEEDED(hr)) hr = AwaitAction(act.Get(), 20000);
  if (SUCCEEDED(hr)) out = mc;
  return hr;
}

// The proven init (TelegArm / the current backend): VideoDeviceId, PhotoCaptureSource=Auto, NO MemoryPreference,
// StreamingCaptureMode AudioAndVideo, falling back to Video.
static HRESULT InitTelegArm(HSTRING id, ComPtr<wmc::IMediaCapture>& mc) {
  static const wmc::StreamingCaptureMode modes[2] = {wmc::StreamingCaptureMode_AudioAndVideo,
                                                     wmc::StreamingCaptureMode_Video};
  static const char* names[2] = {"AudioAndVideo", "Video"};
  HRESULT hr = E_FAIL;
  for (int i = 0; i < 2; i++) {
    STEP(i == 0 ? "init AudioAndVideo" : "init Video");
    ComPtr<wmc::IMediaCaptureInitializationSettings> st;
    hr = NewSettings(st);
    if (FAILED(hr)) return hr;
    st->put_VideoDeviceId(id);
    st->put_PhotoCaptureSource(wmc::PhotoCaptureSource_Auto);
    st->put_StreamingCaptureMode(modes[i]);
    double t = NowMs();
    hr = InitWith(st.Get(), mc);
    P("  init(%s, PhotoCaptureSource=Auto, no MemoryPreference): %s (%.0f ms)\n", names[i], HR(hr), NowMs() - t);
    if (SUCCEEDED(hr) && mc) return hr;
  }
  return hr;
}

// The Microsoft frame-reader recipe minus MemoryPreference: SourceGroup (the group containing this camera),
// SharingMode=ExclusiveControl, StreamingCaptureMode=Video.
static HRESULT InitSourceGroup(HSTRING camId, int camIndex, ComPtr<wmc::IMediaCapture>& mc) {
  STEP("find source group");
  ComPtr<wmcf::IMediaFrameSourceGroupStatics> gs;
  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_Media_Capture_Frames_MediaFrameSourceGroup).Get(), IID_PPV_ARGS(&gs));
  ComPtr<wf::IAsyncOperation<wfc::IVectorView<wmcf::MediaFrameSourceGroup*>*>> op;
  if (SUCCEEDED(hr)) hr = gs->FindAllAsync(&op);
  if (SUCCEEDED(hr)) hr = AwaitOp(op.Get());
  ComPtr<wfc::IVectorView<wmcf::MediaFrameSourceGroup*>> groups;
  if (SUCCEEDED(hr)) hr = op->GetResults(&groups);
  if (FAILED(hr) || !groups) { P("  MediaFrameSourceGroup.FindAllAsync: %s\n", HR(hr)); return FAILED(hr) ? hr : E_FAIL; }
  UINT32 n = 0;
  groups->get_Size(&n);
  ComPtr<wmcf::IMediaFrameSourceGroup> chosen;
  for (UINT32 g = 0; g < n && !chosen; g++) {
    ComPtr<wmcf::IMediaFrameSourceGroup> grp;
    if (FAILED(groups->GetAt(g, &grp)) || !grp) continue;
    ComPtr<wfc::IVectorView<wmcf::MediaFrameSourceInfo*>> infos;
    if (FAILED(grp->get_SourceInfos(&infos)) || !infos) continue;
    UINT32 m = 0;
    infos->get_Size(&m);
    for (UINT32 i = 0; i < m; i++) {
      ComPtr<wmcf::IMediaFrameSourceInfo> info;
      ComPtr<wde::IDeviceInformation> di;
      if (FAILED(infos->GetAt(i, &info)) || !info || FAILED(info->get_DeviceInformation(&di)) || !di) continue;
      HString did;
      di->get_Id(did.GetAddressOf());
      INT32 cmp = 1;
      WindowsCompareStringOrdinal(did.Get(), camId, &cmp);
      if (cmp == 0) { chosen = grp; P("  source group %u contains this camera\n", g); break; }
    }
  }
  if (!chosen && (UINT32)camIndex < n) {
    groups->GetAt((UINT32)camIndex, &chosen);
    P("  no source group matched the camera id; using group %d\n", camIndex);
  }
  if (!chosen) { P("  no source group for this camera (%u groups)\n", n); return E_FAIL; }
  HString gname;
  chosen->get_DisplayName(gname.GetAddressOf());
  STEP("init SourceGroup");
  ComPtr<wmc::IMediaCaptureInitializationSettings> st;
  hr = NewSettings(st);
  ComPtr<wmc::IMediaCaptureInitializationSettings5> st5;
  if (SUCCEEDED(hr)) hr = st.As(&st5);
  if (FAILED(hr)) { P("  IMediaCaptureInitializationSettings5: %s\n", HR(hr)); return hr; }
  st5->put_SourceGroup(chosen.Get());
  st5->put_SharingMode(wmc::MediaCaptureSharingMode_ExclusiveControl);
  st->put_StreamingCaptureMode(wmc::StreamingCaptureMode_Video);
  double t = NowMs();
  hr = InitWith(st.Get(), mc);
  P("  init(SourceGroup \"%s\", ExclusiveControl, Video, no MemoryPreference): %s (%.0f ms)\n", U8(gname.Get()), HR(hr),
    NowMs() - t);
  return hr;
}

static void LogCamState(wmc::IMediaCapture* mc, const char* when) {
  ComPtr<wmc::IMediaCapture4> mc4;
  wmd::CameraStreamState cs = wmd::CameraStreamState_NotStreaming;
  if (mc && SUCCEEDED(mc->QueryInterface(IID_PPV_ARGS(&mc4))) && mc4 && SUCCEEDED(mc4->get_CameraStreamState(&cs)))
    P("  CameraStreamState %s: %s\n", when, CamStateName(cs));
}

// Async failures and stream-state changes - the frame reader can "start" fine and then fail silently without these.
static void AttachDiag(wmc::IMediaCapture* mc) {
  EventRegistrationToken tok;
  auto failed = Callback<wmc::IMediaCaptureFailedEventHandler>(
      [](wmc::IMediaCapture*, wmc::IMediaCaptureFailedEventArgs* a) -> HRESULT {
        UINT32 code = 0;
        HString msg;
        if (a) { a->get_Code(&code); a->get_Message(msg.GetAddressOf()); }
        P("  !! MediaCapture.Failed: code=%s msg=\"%s\" (during '%s', t=%.0f ms)\n", HR((HRESULT)code),
          U8(msg.Get()), g_step, NowMs() - g_t0);
        return S_OK;
      });
  mc->add_Failed(failed.Get(), &tok);
  ComPtr<wmc::IMediaCapture4> mc4;
  if (SUCCEEDED(mc->QueryInterface(IID_PPV_ARGS(&mc4))) && mc4) {
    auto sc = Callback<wf::ITypedEventHandler<wmc::MediaCapture*, IInspectable*>>(
        [](wmc::IMediaCapture* s, IInspectable*) -> HRESULT {
          ComPtr<wmc::IMediaCapture4> m4;
          wmd::CameraStreamState cs = wmd::CameraStreamState_NotStreaming;
          if (s && SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&m4))) && m4) m4->get_CameraStreamState(&cs);
          P("  .. CameraStreamStateChanged -> %s (during '%s', t=%.0f ms)\n", CamStateName(cs), g_step,
            NowMs() - g_t0);
          return S_OK;
        });
    mc4->add_CameraStreamStateChanged(sc.Get(), &tok);
  }
}

static void CloseCapture(ComPtr<wmc::IMediaCapture>& mc) {
  STEP("close MediaCapture");
  CloseIt(mc.Get());
  mc.Reset();
}

// ---------------------------------------------------------------------------------------------------------------------
// frame processing: what the backend will do per frame (lock, copy the bytes out), plus stats. A frame is DISTINCT
// unless it repeats the previous frame's non-zero timestamp or its pixels hash identical to the previous frame's
// (sensor noise makes real consecutive frames differ), so a re-served cached frame never counts.

struct Stats {
  int frames;         // frames whose pixels were read (incl. repeats)
  int d3d, d3dfail;   // frames that arrived as a Direct3DSurface / whose CPU copy failed
  int nopix;          // frames with no readable image at all
  int bufframes;      // BufferMediaFrame frames (compressed payload)
  UINT32 buflen;
  int lockfail;
  double copy_ms, d3dcopy_ms;
  bool described, fmt_logged;
  char fmt[160];
  int w, h, planes;
  UINT32 cap;
  wgi::BitmapPlaneDescription pd[3];
  int luma_first, luma_last, luma_min, luma_max;
  bool px_ok;          // this frame's pixels were read ...
  uint32_t px_hash;    // ... and hash to this
  bool have_hash;
  uint32_t last_hash;
  bool ts_seen;
  INT64 ts_prev;
  int ts_dup, ts0, same;
  int uniq;            // distinct frames
  double first_ms, last_ms, max_gap;
  int dts_n;           // distinct frames carrying a timestamp
  INT64 dts_first, dts_last;
};

static uint8_t* g_scratch;
static UINT32 g_scratch_cap;

// Mean/min/max luma on a sparse grid - tells a live image from black or constant frames.
static void Luma(wgi::BitmapPixelFormat pf, const BYTE* p, UINT32 cap, const wgi::BitmapPlaneDescription& d, int w,
                 int h, int* mean, int* mn, int* mx) {
  int step, off;
  switch (pf) {
    case wgi::BitmapPixelFormat_Nv12: case wgi::BitmapPixelFormat_Gray8: step = 1; off = 0; break;
    case wgi::BitmapPixelFormat_Yuy2: step = 2; off = 0; break;
    case wgi::BitmapPixelFormat_Bgra8: case wgi::BitmapPixelFormat_Rgba8: step = 4; off = 1; break;
    default: *mean = *mn = *mx = -1; return;
  }
  if (w <= 0 || h <= 0 || d.Stride <= 0) { *mean = *mn = *mx = -1; return; }
  int sx = w / 32 > 0 ? w / 32 : 1, sy = h / 24 > 0 ? h / 24 : 1;
  long long sum = 0;
  int cnt = 0, lo = 255, hi = 0;
  for (int y = 0; y < h; y += sy) {
    for (int x = 0; x < w; x += sx) {
      long long i = (long long)d.StartIndex + (long long)y * d.Stride + (long long)x * step + off;
      if (i < 0 || i >= (long long)cap) continue;
      int v = p[i];
      sum += v;
      cnt++;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
  }
  if (!cnt) { *mean = *mn = *mx = -1; return; }
  *mean = (int)(sum / cnt);
  *mn = lo;
  *mx = hi;
}

// FNV-1a over the copied frame (g_scratch is malloc-aligned, so the word loads are aligned on ARM32).
static uint32_t HashBuf(const uint8_t* p, UINT32 n) {
  uint32_t h = 2166136261u;
  const uint32_t* w = (const uint32_t*)p;
  UINT32 k = n / 4;
  for (UINT32 i = 0; i < k; i++) h = (h ^ w[i]) * 16777619u;
  for (UINT32 i = k * 4; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

// Same non-zero timestamp as the previous frame -> the same frame handed out again (skip its pixel work).
static bool SameTs(Stats* st, INT64 ts) {
  if (ts && st->ts_seen && ts == st->ts_prev) { st->ts_dup++; return true; }
  if (ts) { st->ts_prev = ts; st->ts_seen = true; }
  return false;
}

// After a frame's pixels were (maybe) read: count it as distinct unless its content repeats.
static void Account(Stats* st, INT64 ts) {
  if (ts == 0) st->ts0++;
  if (st->px_ok) {
    if (st->have_hash && st->px_hash == st->last_hash) { st->same++; return; }
    st->last_hash = st->px_hash;
    st->have_hash = true;
  }
  double now = NowMs();
  if (st->uniq == 0) st->first_ms = now;
  else if (now - st->last_ms > st->max_gap) st->max_gap = now - st->last_ms;
  st->last_ms = now;
  st->uniq++;
  if (ts) {
    if (st->dts_n == 0) st->dts_first = ts;
    st->dts_last = ts;
    st->dts_n++;
  }
}

static HRESULT ProcessBitmap(wgi::ISoftwareBitmap* sb, Stats* st, const char* via) {
  wgi::BitmapPixelFormat pf = wgi::BitmapPixelFormat_Unknown;
  INT32 w = 0, h = 0;
  sb->get_BitmapPixelFormat(&pf);
  sb->get_PixelWidth(&w);
  sb->get_PixelHeight(&h);
  double t = NowMs();
  ComPtr<wgi::IBitmapBuffer> buf;
  HRESULT hr = sb->LockBuffer(wgi::BitmapBufferAccessMode_Read, &buf);
  if (FAILED(hr) || !buf) {
    if (++st->lockfail <= 3) P("    LockBuffer(%s %dx%d): %s\n", PixName(pf), w, h, HR(hr));
    return FAILED(hr) ? hr : E_FAIL;
  }
  INT32 planes = 0;
  buf->GetPlaneCount(&planes);
  wgi::BitmapPlaneDescription pd[3];
  memset(pd, 0, sizeof pd);
  for (INT32 i = 0; i < planes && i < 3; i++) buf->GetPlaneDescription(i, &pd[i]);
  ComPtr<wf::IMemoryBuffer> mb;
  ComPtr<wf::IMemoryBufferReference> ref;
  ComPtr<::Windows::Foundation::IMemoryBufferByteAccess> ba;
  BYTE* p = nullptr;
  UINT32 cap = 0;
  hr = buf.As(&mb);
  if (SUCCEEDED(hr)) hr = mb->CreateReference(&ref);
  if (SUCCEEDED(hr)) hr = ref.As(&ba);
  if (SUCCEEDED(hr)) hr = ba->GetBuffer(&p, &cap);
  int mean = -1, mn = -1, mx = -1;
  uint32_t hash = 0;
  if (SUCCEEDED(hr) && p && cap) {
    if (cap > g_scratch_cap) {
      free(g_scratch);
      g_scratch = (uint8_t*)malloc(cap);
      g_scratch_cap = g_scratch ? cap : 0;
    }
    if (g_scratch) {
      memcpy(g_scratch, p, cap);  // the backend's copy-out before delivering to Chromium
      hash = HashBuf(g_scratch, cap);
    }
    Luma(pf, p, cap, pd[0], w, h, &mean, &mn, &mx);
  } else if (SUCCEEDED(hr)) {
    hr = E_FAIL;
  }
  CloseIt(ref.Get());
  CloseIt(buf.Get());
  double dt = NowMs() - t;
  if (FAILED(hr)) {
    if (++st->lockfail <= 3) P("    IMemoryBufferByteAccess path (%s): %s\n", via, HR(hr));
    return hr;
  }
  st->frames++;
  st->copy_ms += dt;
  st->px_ok = g_scratch != nullptr;
  st->px_hash = hash;
  if (st->frames == 1) st->luma_first = mean;
  st->luma_last = mean;
  if (!st->described) {
    st->described = true;
    st->w = w;
    st->h = h;
    st->planes = planes;
    st->cap = cap;
    memcpy(st->pd, pd, sizeof pd);
    st->luma_min = mn;
    st->luma_max = mx;
    snprintf(st->fmt, sizeof st->fmt, "%s %dx%d via %s", PixName(pf), w, h, via);
    P("    first pixels: %s, %d plane(s), buffer %u bytes, lock+copy %.2f ms\n", st->fmt, planes, cap, dt);
    for (INT32 i = 0; i < planes && i < 3; i++)
      P("      plane %d: start=%d width=%d height=%d stride=%d\n", i, pd[i].StartIndex, pd[i].Width, pd[i].Height,
        pd[i].Stride);
    P("      luma mean=%d min=%d max=%d %s\n", mean, mn, mx,
      mean < 0 ? "(n/a)" : (mx - mn < 8 ? "<- FLAT: black/blank frame?" : "(live image)"));
  } else {
    if (mn >= 0 && (st->luma_min < 0 || mn < st->luma_min)) st->luma_min = mn;
    if (mx > st->luma_max) st->luma_max = mx;
  }
  return S_OK;
}

static HRESULT ProcessSurface(wgd::IDirect3DSurface* s, Stats* st) {
  wgd::Direct3DSurfaceDescription d;
  memset(&d, 0, sizeof d);
  s->get_Description(&d);
  if (++st->d3d == 1)
    P("    frame is a Direct3DSurface %dx%d DXGI format %d -> SoftwareBitmap.CreateCopyFromSurfaceAsync\n", d.Width,
      d.Height, (int)d.Format);
  double t = NowMs();
  ComPtr<wgi::ISoftwareBitmapStatics> sbs;
  HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap).Get(),
                                      IID_PPV_ARGS(&sbs));
  ComPtr<wf::IAsyncOperation<wgi::SoftwareBitmap*>> op;
  if (SUCCEEDED(hr)) hr = sbs->CreateCopyFromSurfaceAsync(s, &op);
  if (SUCCEEDED(hr)) hr = AwaitOp(op.Get(), 3000);
  ComPtr<wgi::ISoftwareBitmap> sb;
  if (SUCCEEDED(hr)) hr = op->GetResults(&sb);
  double dt = NowMs() - t;
  st->d3dcopy_ms += dt;
  if (st->d3d == 1) P("    first CreateCopyFromSurfaceAsync: %s (%.1f ms)\n", HR(hr), dt);
  if (SUCCEEDED(hr) && sb) hr = ProcessBitmap(sb.Get(), st, "Direct3DSurface->CreateCopyFromSurfaceAsync");
  else if (++st->d3dfail > 1 && st->d3dfail <= 3) P("    CreateCopyFromSurfaceAsync: %s\n", HR(hr));
  CloseIt(sb.Get());
  return hr;
}

static INT64 RefTime(wf::IReference<wf::TimeSpan>* r) {
  wf::TimeSpan v;
  v.Duration = 0;
  if (r && SUCCEEDED(r->get_Value(&v))) return v.Duration;
  return 0;
}

static void ProcessBufferFrame(wmcf::IMediaFrameReference* ref, Stats* st) {
  ComPtr<wmcf::IBufferMediaFrame> bmf;
  ComPtr<wss::IBuffer> b;
  UINT32 len = 0;
  if (SUCCEEDED(ref->get_BufferMediaFrame(&bmf)) && bmf && SUCCEEDED(bmf->get_Buffer(&b)) && b) b->get_Length(&len);
  if (len) {
    if (++st->bufframes == 1) P("    frame is a BufferMediaFrame of %u bytes (compressed payload, no bitmap)\n", len);
    st->buflen = len;
  } else if (++st->nopix == 1) {
    P("    frame has no SoftwareBitmap, Direct3DSurface or BufferMediaFrame\n");
  }
}

static void ProcessFrameRef(wmcf::IMediaFrameReference* ref, Stats* st) {
  ComPtr<wf::IReference<wf::TimeSpan>> tsr;
  ref->get_SystemRelativeTime(&tsr);
  INT64 ts = RefTime(tsr.Get());
  if (SameTs(st, ts)) return;
  st->px_ok = false;
  if (!st->fmt_logged) {
    st->fmt_logged = true;
    ComPtr<wmcf::IMediaFrameFormat> f;
    if (SUCCEEDED(ref->get_Format(&f)) && f) P("    first frame: format %s, timestamp %s\n", DescribeFrameFormat(f.Get()),
                                                ts ? "present" : "ABSENT (dedup by content only)");
  }
  ComPtr<wmcf::IVideoMediaFrame> vmf;
  ComPtr<wgi::ISoftwareBitmap> sb;
  ComPtr<wgd::IDirect3DSurface> surf;
  if (SUCCEEDED(ref->get_VideoMediaFrame(&vmf)) && vmf) {
    vmf->get_SoftwareBitmap(&sb);
    if (!sb) vmf->get_Direct3DSurface(&surf);
  }
  if (sb) ProcessBitmap(sb.Get(), st, "SoftwareBitmap");
  else if (surf) ProcessSurface(surf.Get(), st);
  else ProcessBufferFrame(ref, st);
  Account(st, ts);
}

static void ProcessVideoFrame(wm::IVideoFrame* vf, Stats* st) {
  ComPtr<wm::IMediaFrame> mf;
  INT64 ts = 0;
  if (SUCCEEDED(vf->QueryInterface(IID_PPV_ARGS(&mf))) && mf) {
    ComPtr<wf::IReference<wf::TimeSpan>> r;
    if (SUCCEEDED(mf->get_SystemRelativeTime(&r)) && r) ts = RefTime(r.Get());
    if (!ts) {
      r.Reset();
      if (SUCCEEDED(mf->get_RelativeTime(&r)) && r) ts = RefTime(r.Get());
    }
  }
  if (SameTs(st, ts)) return;
  st->px_ok = false;
  if (!st->fmt_logged) {
    st->fmt_logged = true;
    P("    first frame: timestamp %s\n", ts ? "present" : "ABSENT (dedup by content only)");
  }
  ComPtr<wgi::ISoftwareBitmap> sb;
  vf->get_SoftwareBitmap(&sb);
  ComPtr<wgd::IDirect3DSurface> surf;
  if (!sb) vf->get_Direct3DSurface(&surf);
  if (sb) ProcessBitmap(sb.Get(), st, "VideoFrame.SoftwareBitmap");
  else if (surf) ProcessSurface(surf.Get(), st);
  else if (++st->nopix == 1) P("    VideoFrame has neither a SoftwareBitmap nor a Direct3DSurface\n");
  Account(st, ts);
}

// Sustained rate: n events over [first, end] - a burst followed by a stall reads low, as it should.
static double Sustained(int n, double first, double end) {
  return (n > 0 && end > first) ? n * 1000.0 / (end - first) : 0.0;
}

static double CamTsFps(const Stats& st) {
  return (st.dts_n >= 2 && st.dts_last > st.dts_first) ? (st.dts_n - 1) * 1e7 / (double)(st.dts_last - st.dts_first)
                                                         : 0.0;
}

static bool Flat(const Stats& st) { return st.frames > 0 && st.luma_max >= 0 && st.luma_max - st.luma_min < 8; }

// "STREAMS", "STREAMS(no-CPU-pixels)", "SLOW(FLAT-image?)", "NO-FRAMES", ...
static const char* VerdictEx(double fps, bool anyFrames, const Stats& st) {
  char* b = Slot();
  const char* v = !anyFrames ? "NO-FRAMES" : (fps >= 10.0 ? "STREAMS" : "SLOW");
  snprintf(b, 600, "%s%s%s", v, (anyFrames && st.frames == 0) ? "(no-CPU-pixels)" : "", Flat(st) ? "(FLAT-image?)" : "");
  return b;
}
// What the WINNER line ranks by: only a primitive that delivered readable, live pixels scores.
static double Score(double fps, const Stats& st) { return (st.frames > 0 && !Flat(st)) ? fps : 0.0; }

static const char* StatsLine(const Stats& st) {
  char* b = Slot();
  snprintf(b, 600,
           "fmt=%s pixels=%d same=%d ts_dup=%d ts0=%d max_gap=%.0fms cam_ts_fps=%.1f copy_avg=%.2fms d3d=%d(avg %.1fms, "
           "fail %d) buf=%d nopix=%d lockfail=%d luma first=%d last=%d range=%d..%d",
           st.described ? st.fmt : "-", st.frames, st.same, st.ts_dup, st.ts0, st.max_gap, CamTsFps(st),
           st.frames ? st.copy_ms / st.frames : 0.0, st.d3d, st.d3d ? st.d3dcopy_ms / st.d3d : 0.0, st.d3dfail,
           st.bufframes, st.nopix, st.lockfail, st.luma_first, st.luma_last, st.luma_min, st.luma_max);
  return b;
}

// ---------------------------------------------------------------------------------------------------------------------
// frame sources

static void DumpSource(wmcf::IMediaFrameSource* src, int idx, bool formats) {
  ComPtr<wmcf::IMediaFrameSourceInfo> info;
  src->get_Info(&info);
  HString id;
  wmc::MediaStreamType t = wmc::MediaStreamType_VideoPreview;
  wmcf::MediaFrameSourceKind k = wmcf::MediaFrameSourceKind_Custom;
  if (info) { info->get_Id(id.GetAddressOf()); info->get_MediaStreamType(&t); info->get_SourceKind(&k); }
  ComPtr<wmcf::IMediaFrameFormat> cur;
  src->get_CurrentFormat(&cur);
  P("    source[%d]: %s / %s  current=%s  id=%s\n", idx, KindName(k), StreamTypeName(t), DescribeFrameFormat(cur.Get()),
    U8(id.Get()));
  if (!formats) return;
  ComPtr<wfc::IVectorView<wmcf::MediaFrameFormat*>> fl;
  if (FAILED(src->get_SupportedFormats(&fl)) || !fl) return;
  UINT32 n = 0;
  fl->get_Size(&n);
  for (UINT32 i = 0; i < n; i++) {
    ComPtr<wmcf::IMediaFrameFormat> f;
    if (SUCCEEDED(fl->GetAt(i, &f)) && f) P("      fmt[%u] %s\n", i, DescribeFrameFormat(f.Get()));
  }
}

// Pick the Color source of stream type |want| (else the first Color source, logged). |dump| lists all sources.
static HRESULT FindSource(wmc::IMediaCapture* mc, wmc::MediaStreamType want, ComPtr<wmcf::IMediaFrameSource>& out,
                          bool dump, wmc::MediaStreamType* got) {
  STEP("find frame source");
  ComPtr<wmc::IMediaCapture5> mc5;
  HRESULT hr = mc->QueryInterface(IID_PPV_ARGS(&mc5));
  if (FAILED(hr)) { P("  IMediaCapture5 (frame sources): %s\n", HR(hr)); return hr; }
  ComPtr<wfc::IMapView<HSTRING, wmcf::MediaFrameSource*>> map;
  hr = mc5->get_FrameSources(&map);
  if (FAILED(hr) || !map) { P("  FrameSources: %s\n", HR(hr)); return FAILED(hr) ? hr : E_FAIL; }
  UINT32 size = 0;
  map->get_Size(&size);
  if (dump) P("  FrameSources: %u\n", size);
  ComPtr<wfc::IIterable<wfc::IKeyValuePair<HSTRING, wmcf::MediaFrameSource*>*>> it;
  hr = map.As(&it);
  ComPtr<wfc::IIterator<wfc::IKeyValuePair<HSTRING, wmcf::MediaFrameSource*>*>> cur;
  if (SUCCEEDED(hr)) hr = it->First(&cur);
  if (FAILED(hr) || !cur) return FAILED(hr) ? hr : E_FAIL;
  boolean has = false;
  cur->get_HasCurrent(&has);
  ComPtr<wmcf::IMediaFrameSource> exact, anyColor;
  wmc::MediaStreamType anyType = wmc::MediaStreamType_VideoPreview;
  int idx = 0;
  while (has) {
    ComPtr<wfc::IKeyValuePair<HSTRING, wmcf::MediaFrameSource*>> kv;
    ComPtr<wmcf::IMediaFrameSource> src;
    if (SUCCEEDED(cur->get_Current(&kv)) && kv && SUCCEEDED(kv->get_Value(&src)) && src) {
      if (dump) DumpSource(src.Get(), idx, true);
      ComPtr<wmcf::IMediaFrameSourceInfo> info;
      wmc::MediaStreamType t = wmc::MediaStreamType_Audio;
      wmcf::MediaFrameSourceKind k = wmcf::MediaFrameSourceKind_Custom;
      if (SUCCEEDED(src->get_Info(&info)) && info) { info->get_MediaStreamType(&t); info->get_SourceKind(&k); }
      if (k == wmcf::MediaFrameSourceKind_Color) {
        if (!anyColor) { anyColor = src; anyType = t; }
        if (t == want && !exact) exact = src;
      }
    }
    idx++;
    if (FAILED(cur->MoveNext(&has))) break;
  }
  out = exact ? exact : anyColor;
  if (got) *got = exact ? want : anyType;
  if (!out) { P("  no Color frame source\n"); return E_FAIL; }
  if (!exact) P("  !! no %s Color source; FALLING BACK to the first Color source (%s)\n", StreamTypeName(want),
                StreamTypeName(anyType));
  return S_OK;
}

// SetFormatAsync to the best format fitting 640x480 (uncompressed first, then >= 24 fps).
static HRESULT SetSourceFormat640(wmcf::IMediaFrameSource* src) {
  STEP("SetFormatAsync");
  ComPtr<wfc::IVectorView<wmcf::MediaFrameFormat*>> fl;
  HRESULT hr = src->get_SupportedFormats(&fl);
  if (FAILED(hr) || !fl) { P("  SupportedFormats: %s\n", HR(hr)); return FAILED(hr) ? hr : E_FAIL; }
  UINT32 n = 0;
  fl->get_Size(&n);
  Pick best;
  memset(&best, 0, sizeof best);
  ComPtr<wmcf::IMediaFrameFormat> chosen;
  for (UINT32 i = 0; i < n; i++) {
    ComPtr<wmcf::IMediaFrameFormat> f;
    if (FAILED(fl->GetAt(i, &f)) || !f) continue;
    UINT32 w = 0, h = 0;
    ComPtr<wmcf::IVideoMediaFrameFormat> vf;
    if (SUCCEEDED(f->get_VideoFormat(&vf)) && vf) { vf->get_Width(&w); vf->get_Height(&h); }
    ComPtr<wmp::IMediaRatio> r;
    f->get_FrameRate(&r);
    HString sub;
    f->get_Subtype(sub.GetAddressOf());
    if (w && h && Better(&best, w, h, RatioFps(r.Get()), Uncompressed(sub.Get()))) chosen = f;
  }
  if (!chosen) { P("  no usable format among %u\n", n); return E_FAIL; }
  ComPtr<wf::IAsyncAction> act;
  double t = NowMs();
  hr = src->SetFormatAsync(chosen.Get(), &act);
  if (SUCCEEDED(hr)) hr = AwaitAction(act.Get());
  P("  SetFormatAsync(%s): %s (%.0f ms)\n", DescribeFrameFormat(chosen.Get()), HR(hr), NowMs() - t);
  ComPtr<wmcf::IMediaFrameFormat> cur;
  src->get_CurrentFormat(&cur);
  P("  current format now: %s\n", DescribeFrameFormat(cur.Get()));
  return hr;
}

// VideoDeviceController: list (optionally) + current for one stream type
static void DumpStreamProps(wmd::IMediaDeviceController* mdc, wmc::MediaStreamType t, bool list) {
  ComPtr<wmp::IMediaEncodingProperties> cur;
  mdc->GetMediaStreamProperties(t, &cur);
  P("  %s current: %s\n", StreamTypeName(t), DescribeProps(cur.Get()));
  if (!list) return;
  ComPtr<wfc::IVectorView<wmp::IMediaEncodingProperties*>> props;
  if (FAILED(mdc->GetAvailableMediaStreamProperties(t, &props)) || !props) return;
  UINT32 n = 0;
  props->get_Size(&n);
  P("  %s available: %u\n", StreamTypeName(t), n);
  for (UINT32 i = 0; i < n; i++) {
    ComPtr<wmp::IMediaEncodingProperties> p;
    if (SUCCEEDED(props->GetAt(i, &p)) && p) P("      [%u] %s\n", i, DescribeProps(p.Get()));
  }
}

static HRESULT GetMdc(wmc::IMediaCapture* mc, ComPtr<wmd::IMediaDeviceController>& mdc) {
  ComPtr<wmd::IVideoDeviceController> vdc;
  HRESULT hr = mc->get_VideoDeviceController(&vdc);
  if (SUCCEEDED(hr) && vdc) hr = vdc.As(&mdc);
  return SUCCEEDED(hr) && mdc ? S_OK : (FAILED(hr) ? hr : E_FAIL);
}

// SetMediaStreamPropertiesAsync(|type|) to the best video props fitting 640x480.
static HRESULT SetStream640(wmd::IMediaDeviceController* mdc, wmc::MediaStreamType type) {
  STEP("SetMediaStreamPropertiesAsync");
  ComPtr<wfc::IVectorView<wmp::IMediaEncodingProperties*>> props;
  HRESULT hr = mdc->GetAvailableMediaStreamProperties(type, &props);
  if (FAILED(hr) || !props) { P("  GetAvailableMediaStreamProperties(%s): %s\n", StreamTypeName(type), HR(hr)); return hr; }
  UINT32 n = 0;
  props->get_Size(&n);
  Pick best;
  memset(&best, 0, sizeof best);
  ComPtr<wmp::IMediaEncodingProperties> chosen;
  for (UINT32 i = 0; i < n; i++) {
    ComPtr<wmp::IMediaEncodingProperties> p;
    if (FAILED(props->GetAt(i, &p)) || !p) continue;
    UINT32 w = 0, h = 0;
    double fps = 0;
    DescribeProps(p.Get(), &w, &h, &fps);
    HString sub;
    p->get_Subtype(sub.GetAddressOf());
    if (w && h && Better(&best, w, h, fps, Uncompressed(sub.Get()))) chosen = p;
  }
  if (!chosen) { P("  no usable %s props among %u\n", StreamTypeName(type), n); return E_FAIL; }
  ComPtr<wf::IAsyncAction> act;
  double t = NowMs();
  hr = mdc->SetMediaStreamPropertiesAsync(type, chosen.Get(), &act);
  if (SUCCEEDED(hr)) hr = AwaitAction(act.Get());
  P("  SetMediaStreamPropertiesAsync(%s, %s): %s (%.0f ms)\n", StreamTypeName(type), DescribeProps(chosen.Get()), HR(hr),
    NowMs() - t);
  return hr;
}

// ---------------------------------------------------------------------------------------------------------------------
// tests

static int g_secs = 4;

// info: everything the device reports, no streaming.
static int TestInfo(HSTRING camId) {
  STEP("source groups");
  ComPtr<wmcf::IMediaFrameSourceGroupStatics> gs;
  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_Media_Capture_Frames_MediaFrameSourceGroup).Get(), IID_PPV_ARGS(&gs));
  ComPtr<wf::IAsyncOperation<wfc::IVectorView<wmcf::MediaFrameSourceGroup*>*>> op;
  if (SUCCEEDED(hr)) hr = gs->FindAllAsync(&op);
  if (SUCCEEDED(hr)) hr = AwaitOp(op.Get());
  ComPtr<wfc::IVectorView<wmcf::MediaFrameSourceGroup*>> groups;
  if (SUCCEEDED(hr)) hr = op->GetResults(&groups);
  UINT32 ng = 0;
  if (groups) groups->get_Size(&ng);
  P("  MediaFrameSourceGroup.FindAllAsync: %s, %u group(s)\n", HR(hr), ng);
  for (UINT32 g = 0; g < ng; g++) {
    ComPtr<wmcf::IMediaFrameSourceGroup> grp;
    if (FAILED(groups->GetAt(g, &grp)) || !grp) continue;
    HString name;
    grp->get_DisplayName(name.GetAddressOf());
    P("    group[%u] \"%s\"\n", g, U8(name.Get()));
    ComPtr<wfc::IVectorView<wmcf::MediaFrameSourceInfo*>> infos;
    if (FAILED(grp->get_SourceInfos(&infos)) || !infos) continue;
    UINT32 m = 0;
    infos->get_Size(&m);
    for (UINT32 i = 0; i < m; i++) {
      ComPtr<wmcf::IMediaFrameSourceInfo> info;
      if (FAILED(infos->GetAt(i, &info)) || !info) continue;
      wmc::MediaStreamType t = wmc::MediaStreamType_VideoPreview;
      wmcf::MediaFrameSourceKind k = wmcf::MediaFrameSourceKind_Custom;
      info->get_MediaStreamType(&t);
      info->get_SourceKind(&k);
      P("      info[%u] %s / %s\n", i, KindName(k), StreamTypeName(t));
    }
  }

  ComPtr<wmc::IMediaCapture> mc;
  hr = InitTelegArm(camId, mc);
  if (FAILED(hr) || !mc) { Result("MediaCapture init failed %s", HR(hr)); return 1; }
  AttachDiag(mc.Get());
  LogCamState(mc.Get(), "after init");
  STEP("settings");
  const char* chr = "?";
  ComPtr<wmc::IMediaCaptureSettings> mcs;
  if (SUCCEEDED(mc->get_MediaCaptureSettings(&mcs)) && mcs) {
    wmc::VideoDeviceCharacteristic c = wmc::VideoDeviceCharacteristic_AllStreamsIndependent;
    wmc::StreamingCaptureMode m = wmc::StreamingCaptureMode_AudioAndVideo;
    mcs->get_VideoDeviceCharacteristic(&c);
    mcs->get_StreamingCaptureMode(&m);
    chr = CharacteristicName(c);
    P("  VideoDeviceCharacteristic: %s   StreamingCaptureMode: %d\n", chr, (int)m);
    ComPtr<wmc::IMediaCaptureSettings2> mcs2;
    if (SUCCEEDED(mcs.As(&mcs2)) && mcs2) {
      boolean crp = false;
      mcs2->get_ConcurrentRecordAndPhotoSupported(&crp);
      P("  ConcurrentRecordAndPhotoSupported: %d\n", (int)crp);
    }
  }
  ComPtr<wmcf::IMediaFrameSource> src;
  FindSource(mc.Get(), wmc::MediaStreamType_VideoPreview, src, true, nullptr);
  STEP("device controller");
  ComPtr<wmd::IMediaDeviceController> mdc;
  if (SUCCEEDED(GetMdc(mc.Get(), mdc))) {
    DumpStreamProps(mdc.Get(), wmc::MediaStreamType_VideoPreview, true);
    DumpStreamProps(mdc.Get(), wmc::MediaStreamType_VideoRecord, true);
    DumpStreamProps(mdc.Get(), wmc::MediaStreamType_Photo, true);
  }
  Result("ok - VideoDeviceCharacteristic=%s", chr);
  CloseCapture(mc);
  return 0;
}

static HRESULT StartPreview(wmc::IMediaCapture* mc, ComPtr<wmc::IMediaCaptureVideoPreview>& pv) {
  STEP("StartPreviewAsync");
  HRESULT hr = mc->QueryInterface(IID_PPV_ARGS(&pv));
  ComPtr<wf::IAsyncAction> act;
  double t = NowMs();
  if (SUCCEEDED(hr)) hr = pv->StartPreviewAsync(&act);
  if (SUCCEEDED(hr)) hr = AwaitAction(act.Get());
  P("  StartPreviewAsync (headless, no CaptureElement): %s (%.0f ms)\n", HR(hr), NowMs() - t);
  return hr;
}

static void StopPreview(wmc::IMediaCaptureVideoPreview* pv) {
  STEP("StopPreviewAsync");
  ComPtr<wf::IAsyncAction> stop;
  if (pv && SUCCEEDED(pv->StopPreviewAsync(&stop))) AwaitAction(stop.Get(), 5000);
}

// A: MediaFrameReader
static volatile LONG g_arrivals;
static double g_arrT[8192];
static HANDLE g_frameEvt;

struct FrOpts { wmc::MediaStreamType want; bool set640; const wchar_t* subtype; bool sourceGroup; bool withPreview; };

static int TestFrameReader(const FrOpts& o, HSTRING camId, int camIndex) {
  ComPtr<wmc::IMediaCapture> mc;
  HRESULT hr = o.sourceGroup ? InitSourceGroup(camId, camIndex, mc) : InitTelegArm(camId, mc);
  if (FAILED(hr) || !mc) { Result("NO-FRAMES MediaCapture init failed %s", HR(hr)); return 1; }
  AttachDiag(mc.Get());
  ComPtr<wmc::IMediaCaptureVideoPreview> pv;
  const char* previewState = "off";
  if (o.withPreview) previewState = SUCCEEDED(StartPreview(mc.Get(), pv)) ? "running" : "FAILED-to-start";
  ComPtr<wmcf::IMediaFrameSource> src;
  wmc::MediaStreamType got = o.want;
  hr = FindSource(mc.Get(), o.want, src, false, &got);
  if (FAILED(hr) || !src) { Result("NO-FRAMES no frame source %s", HR(hr)); CloseCapture(mc); return 1; }
  DumpSource(src.Get(), -1, false);
  if (o.set640) SetSourceFormat640(src.Get());  // best effort: a failure still runs the reader on the current format
  ComPtr<wmcf::IMediaFrameFormat> curFmt;
  src->get_CurrentFormat(&curFmt);
  const char* srcDesc = DescribeFrameFormat(curFmt.Get());
  char srcLine[200];
  snprintf(srcLine, sizeof srcLine, "src=%s[%s]%s%ls preview=%s", StreamTypeName(got), srcDesc,
           o.subtype ? " out=" : "", o.subtype ? o.subtype : L"", previewState);

  STEP("CreateFrameReaderAsync");
  ComPtr<wmc::IMediaCapture5> mc5;
  mc.As(&mc5);
  ComPtr<wf::IAsyncOperation<wmcf::MediaFrameReader*>> cop;
  double t = NowMs();
  if (o.subtype) hr = mc5->CreateFrameReaderWithSubtypeAsync(src.Get(), HStringReference(o.subtype).Get(), &cop);
  else hr = mc5->CreateFrameReaderAsync(src.Get(), &cop);
  if (SUCCEEDED(hr)) hr = AwaitOp(cop.Get());
  ComPtr<wmcf::IMediaFrameReader> reader;
  if (SUCCEEDED(hr)) hr = cop->GetResults(&reader);
  P("  CreateFrameReader%s%ls%s: %s (%.0f ms)\n", o.subtype ? "WithSubtypeAsync(" : "Async",
    o.subtype ? o.subtype : L"", o.subtype ? ")" : "", HR(hr), NowMs() - t);
  if (FAILED(hr) || !reader) {
    Result("NO-FRAMES CreateFrameReader failed %s %s", HR(hr), srcLine);
    if (pv && o.withPreview) StopPreview(pv.Get());
    CloseCapture(mc);
    return 1;
  }

  g_frameEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  auto handler = Callback<wf::ITypedEventHandler<wmcf::MediaFrameReader*, wmcf::MediaFrameArrivedEventArgs*>>(
      [](wmcf::IMediaFrameReader*, wmcf::IMediaFrameArrivedEventArgs*) -> HRESULT {
        LONG i = InterlockedIncrement(&g_arrivals);
        if (i <= (LONG)(sizeof g_arrT / sizeof g_arrT[0])) g_arrT[i - 1] = NowMs();
        SetEvent(g_frameEvt);
        return S_OK;
      });
  EventRegistrationToken tok;
  tok.value = 0;
  hr = reader->add_FrameArrived(handler.Get(), &tok);
  if (FAILED(hr)) P("  add_FrameArrived: %s\n", HR(hr));

  STEP("MediaFrameReader.StartAsync");
  ComPtr<wf::IAsyncOperation<wmcf::MediaFrameReaderStartStatus>> sop;
  double tStart = NowMs();
  hr = reader->StartAsync(&sop);
  if (SUCCEEDED(hr)) hr = AwaitOp(sop.Get());
  wmcf::MediaFrameReaderStartStatus ss = wmcf::MediaFrameReaderStartStatus_UnknownFailure;
  if (SUCCEEDED(hr)) sop->GetResults(&ss);
  double tStarted = NowMs();
  P("  StartAsync: %s status=%s (%.0f ms)\n", HR(hr), StartStatusName(ss), tStarted - tStart);
  char startTxt[64];
  snprintf(startTxt, sizeof startTxt, "%s", SUCCEEDED(hr) ? StartStatusName(ss) : HR(hr));
  startTxt[24] = 0;

  // Wait up to FIRST_FRAME_WAIT_MS for the first frame (event or poll), then measure g_secs from that frame. The 10 ms
  // wait also polls TryAcquireLatestFrame, so frames are seen even if FrameArrived never fires.
  Stats st;
  memset(&st, 0, sizeof st);
  st.luma_min = -1;
  int acquired = 0;
  double first = -1, end = 0;
  if (SUCCEEDED(hr) && ss == wmcf::MediaFrameReaderStartStatus_Success) {
    STEP("waiting for the first frame");
    for (;;) {
      double now = NowMs();
      if (first < 0 && now > tStarted + FIRST_FRAME_WAIT_MS) break;
      if (first >= 0 && now > end) break;
      WaitForSingleObject(g_frameEvt, 10);
      if (first < 0 && g_arrivals > 0) {
        first = g_arrT[0] > 0 ? g_arrT[0] : NowMs();
        end = first + g_secs * 1000.0;
        STEP("reading frames");
      }
      ComPtr<wmcf::IMediaFrameReference> ref;
      if (FAILED(reader->TryAcquireLatestFrame(&ref)) || !ref) continue;
      acquired++;
      if (first < 0) { first = NowMs(); end = first + g_secs * 1000.0; STEP("reading frames"); }
      ProcessFrameRef(ref.Get(), &st);
      CloseIt(ref.Get());
    }
  }
  LogCamState(mc.Get(), "after reading");

  // Analyse + report BEFORE teardown (a Stop/Close hang must not lose the measurement).
  LONG n = g_arrivals;
  LONG cap = (LONG)(sizeof g_arrT / sizeof g_arrT[0]);
  LONG nt = n < cap ? n : cap;
  int inWin = 0;
  double gapA = 0, prevA = -1;
  for (LONG i = 0; i < nt && first >= 0; i++) {
    double ta = g_arrT[i];
    if (ta <= 0 || ta < first - 0.5 || ta > end) continue;
    inWin++;
    if (prevA >= 0 && ta - prevA > gapA) gapA = ta - prevA;
    prevA = ta;
  }
  double arriveFps = first >= 0 ? Sustained(inWin, first, end) : 0.0;
  double distinctFps = first >= 0 ? Sustained(st.uniq, first, end) : 0.0;
  double primFps = inWin > 0 ? arriveFps : distinctFps;  // the primitive's own rate (not our processing rate)
  bool any = n > 0 || acquired > 0;
  P("  FrameArrived events: %ld total, %d in the %d s window (max gap %.0f ms); first frame %.0f ms after StartAsync; "
    "acquired %d, distinct %d\n", n, inWin, g_secs, gapA, first >= 0 ? first - tStarted : -1.0, acquired, st.uniq);
  Result("%s SCORE=%.1f arrive_fps=%.1f distinct_fps=%.1f first=%.0fms start=%s %s arrived=%ld distinct=%d %s",
         VerdictEx(primFps, any, st), Score(primFps, st), arriveFps, distinctFps,
         first >= 0 ? first - tStarted : -1.0, startTxt, srcLine, n, st.uniq, StatsLine(st));

  STEP("MediaFrameReader.StopAsync");
  t = NowMs();
  reader->remove_FrameArrived(tok);
  ComPtr<wf::IAsyncAction> stop;
  if (SUCCEEDED(reader->StopAsync(&stop))) AwaitAction(stop.Get(), 5000);
  STEP("close MediaFrameReader");
  CloseIt(reader.Get());
  reader.Reset();
  if (pv && o.withPreview) StopPreview(pv.Get());
  CloseCapture(mc);
  P("  teardown ok (%.0f ms)\n", NowMs() - t);
  return 0;
}

// B: StartPreviewAsync + GetPreviewFrameAsync polling
static int TestPreview(HSTRING camId, bool set640, bool copy) {
  ComPtr<wmc::IMediaCapture> mc;
  HRESULT hr = InitTelegArm(camId, mc);
  if (FAILED(hr) || !mc) { Result("NO-FRAMES MediaCapture init failed %s", HR(hr)); return 1; }
  AttachDiag(mc.Get());
  ComPtr<wmd::IMediaDeviceController> mdc;
  UINT32 pw = 0, ph = 0;
  char propsTxt[128] = "?";
  if (SUCCEEDED(GetMdc(mc.Get(), mdc))) {
    if (set640) SetStream640(mdc.Get(), wmc::MediaStreamType_VideoPreview);
    ComPtr<wmp::IMediaEncodingProperties> cur;
    mdc->GetMediaStreamProperties(wmc::MediaStreamType_VideoPreview, &cur);
    snprintf(propsTxt, sizeof propsTxt, "%s", DescribeProps(cur.Get(), &pw, &ph));
    P("  VideoPreview stream: %s\n", propsTxt);
  }
  ComPtr<wmc::IMediaCapture4> mc4;
  mc.As(&mc4);
  ComPtr<wmc::IMediaCaptureVideoPreview> pv;
  hr = mc4 ? StartPreview(mc.Get(), pv) : E_NOINTERFACE;
  if (FAILED(hr)) { Result("NO-FRAMES StartPreviewAsync failed %s preview=[%s]", HR(hr), propsTxt); CloseCapture(mc); return 1; }
  double startDone = NowMs();
  LogCamState(mc.Get(), "after StartPreviewAsync");

  ComPtr<wm::IVideoFrame> dest;
  if (copy) {
    STEP("VideoFrame.Create");
    ComPtr<wm::IVideoFrameFactory> vff;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Media_VideoFrame).Get(), IID_PPV_ARGS(&vff));
    INT32 cw = pw ? (INT32)pw : 640, ch = ph ? (INT32)ph : 480;
    if (SUCCEEDED(hr)) hr = vff->Create(wgi::BitmapPixelFormat_Bgra8, cw, ch, &dest);
    P("  VideoFrame.Create(Bgra8, %dx%d): %s\n", cw, ch, HR(hr));
    if (FAILED(hr) || !dest) {
      Result("NO-FRAMES VideoFrame.Create failed %s", HR(hr));
      StopPreview(pv.Get());
      CloseCapture(mc);
      return 1;
    }
  }

  // Poll until the first frame (up to FIRST_FRAME_WAIT_MS), then for g_secs from that frame.
  STEP(copy ? "GetPreviewFrameCopyAsync: waiting for the first frame" : "GetPreviewFrameAsync: waiting for the first frame");
  Stats st;
  memset(&st, 0, sizeof st);
  st.luma_min = -1;
  int calls = 0, ok = 0, fails = 0, errsLogged = 0;
  double callms = 0, maxcall = 0, first = -1, end = 0;
  HRESULT lastErr = S_OK;
  for (;;) {
    double now = NowMs();
    if (first < 0 && now > startDone + FIRST_FRAME_WAIT_MS) break;
    if (first >= 0 && now > end) break;
    ComPtr<wf::IAsyncOperation<wm::VideoFrame*>> op;
    double c0 = NowMs();
    hr = copy ? mc4->GetPreviewFrameCopyAsync(dest.Get(), &op) : mc4->GetPreviewFrameAsync(&op);
    if (SUCCEEDED(hr)) hr = AwaitOp(op.Get(), 3000);
    ComPtr<wm::IVideoFrame> vf;
    if (SUCCEEDED(hr)) hr = op->GetResults(&vf);
    double cd = NowMs() - c0;
    calls++;
    callms += cd;
    if (cd > maxcall) maxcall = cd;
    if (FAILED(hr) || !vf) {
      fails++;
      if ((fails == 1 || hr != lastErr) && errsLogged < 5) {
        errsLogged++;
        P("    GetPreviewFrame%sAsync: %s (after %.0f ms)\n", copy ? "Copy" : "", HR(hr), NowMs() - startDone);
      }
      lastErr = hr;
      Sleep(first < 0 ? 50 : 20);
      continue;
    }
    if (first < 0) {
      first = NowMs();
      end = first + g_secs * 1000.0;
      STEP(copy ? "GetPreviewFrameCopyAsync loop" : "GetPreviewFrameAsync loop");
    }
    ok++;
    ProcessVideoFrame(vf.Get(), &st);
    if (!copy) CloseIt(vf.Get());
  }
  LogCamState(mc.Get(), "after polling");
  double callFps = first >= 0 ? Sustained(ok, first, end) : 0.0;
  double distinctFps = first >= 0 ? Sustained(st.uniq, first, end) : 0.0;
  Result("%s SCORE=%.1f distinct_fps=%.1f call_fps=%.1f first=%.0fms preview=[%s]%s calls=%d ok=%d fails=%d "
         "call_avg=%.0fms max=%.0fms distinct=%d %s",
         VerdictEx(distinctFps, ok > 0, st), Score(distinctFps, st), distinctFps, callFps,
         first >= 0 ? first - startDone : -1.0, propsTxt, copy ? " copy=Bgra8" : "", calls, ok, fails,
         calls ? callms / calls : 0.0, maxcall, st.uniq, StatsLine(st));
  double t = NowMs();
  StopPreview(pv.Get());
  CloseIt(dest.Get());
  dest.Reset();
  CloseCapture(mc);
  P("  teardown ok (%.0f ms)\n", NowMs() - t);
  return 0;
}

// ---- MP4 parsing for the record test (byte-wise, no unaligned loads)
static uint32_t BE32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t BE64(const uint8_t* p) { return ((uint64_t)BE32(p) << 32) | BE32(p + 4); }
struct Box { const uint8_t* body; size_t len; char type[5]; };
static bool NextBox(const uint8_t* p, size_t n, size_t* off, Box* b) {
  if (*off + 8 > n) return false;
  const uint8_t* q = p + *off;
  uint64_t size = BE32(q);
  size_t hdr = 8;
  memcpy(b->type, q + 4, 4);
  b->type[4] = 0;
  if (size == 1) {
    if (*off + 16 > n) return false;
    size = BE64(q + 8);
    hdr = 16;
  } else if (size == 0) {
    size = n - *off;
  }
  if (size < hdr || size > (uint64_t)(n - *off)) return false;
  b->body = q + hdr;
  b->len = (size_t)size - hdr;
  *off += (size_t)size;
  return true;
}
static bool FindChild(const uint8_t* p, size_t n, const char* type, Box* out) {
  size_t off = 0;
  Box b;
  while (NextBox(p, n, &off, &b))
    if (!memcmp(b.type, type, 4)) { *out = b; return true; }
  return false;
}
struct Mp4Video { bool found; uint32_t track_id, samples, w, h; double secs; uint32_t frag_samples; };
static Mp4Video ParseMp4(const uint8_t* p, size_t n) {
  Mp4Video v;
  memset(&v, 0, sizeof v);
  Box moov;
  if (!FindChild(p, n, "moov", &moov)) return v;
  size_t off = 0;
  Box trak;
  while (NextBox(moov.body, moov.len, &off, &trak)) {
    if (memcmp(trak.type, "trak", 4)) continue;
    Box mdia, hdlr, mdhd, minf, stbl, stsz, tkhd;
    if (!FindChild(trak.body, trak.len, "mdia", &mdia)) continue;
    if (!FindChild(mdia.body, mdia.len, "hdlr", &hdlr) || hdlr.len < 12 || memcmp(hdlr.body + 8, "vide", 4)) continue;
    v.found = true;
    if (FindChild(trak.body, trak.len, "tkhd", &tkhd) && tkhd.len >= 84) {
      bool v1 = tkhd.body[0] == 1;
      v.track_id = BE32(tkhd.body + (v1 ? 20 : 12));
      size_t o = v1 ? 88 : 76;
      if (tkhd.len >= o + 8) { v.w = BE32(tkhd.body + o) >> 16; v.h = BE32(tkhd.body + o + 4) >> 16; }
    }
    if (FindChild(mdia.body, mdia.len, "mdhd", &mdhd) && mdhd.len >= 20) {
      uint32_t ts = 0;
      uint64_t dur = 0;
      if (mdhd.body[0] == 1) {
        if (mdhd.len >= 32) { ts = BE32(mdhd.body + 20); dur = BE64(mdhd.body + 24); }
      } else {
        ts = BE32(mdhd.body + 12);
        dur = BE32(mdhd.body + 16);
      }
      v.secs = ts ? (double)dur / ts : 0.0;
    }
    if (FindChild(mdia.body, mdia.len, "minf", &minf) && FindChild(minf.body, minf.len, "stbl", &stbl) &&
        FindChild(stbl.body, stbl.len, "stsz", &stsz) && stsz.len >= 12)
      v.samples = BE32(stsz.body + 8);
    break;
  }
  // fragmented MP4: sum trun sample counts of the video track's fragments
  off = 0;
  Box top;
  while (NextBox(p, n, &off, &top)) {
    if (memcmp(top.type, "moof", 4)) continue;
    size_t o2 = 0;
    Box traf;
    while (NextBox(top.body, top.len, &o2, &traf)) {
      if (memcmp(traf.type, "traf", 4)) continue;
      Box tfhd, trun;
      if (FindChild(traf.body, traf.len, "tfhd", &tfhd) && tfhd.len >= 8 && v.track_id &&
          BE32(tfhd.body + 4) != v.track_id)
        continue;
      size_t o3 = 0;
      while (NextBox(traf.body, traf.len, &o3, &trun))
        if (!memcmp(trun.type, "trun", 4) && trun.len >= 8) v.frag_samples += BE32(trun.body + 4);
    }
  }
  return v;
}

static HRESULT ReadAll(wss::IRandomAccessStream* ras, uint8_t** out, UINT32* outLen) {
  *out = nullptr;
  *outLen = 0;
  UINT64 size = 0;
  ras->get_Size(&size);
  if (size == 0) return S_FALSE;
  if (size > 256u * 1024 * 1024) return E_OUTOFMEMORY;
  ComPtr<wss::IInputStream> in;
  HRESULT hr = ras->GetInputStreamAt(0, &in);
  ComPtr<wss::IDataReaderFactory> f;
  if (SUCCEEDED(hr))
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_Streams_DataReader).Get(),
                                IID_PPV_ARGS(&f));
  ComPtr<wss::IDataReader> rd;
  if (SUCCEEDED(hr)) hr = f->CreateDataReader(in.Get(), &rd);
  ComPtr<wf::IAsyncOperation<UINT32>> ld;
  if (SUCCEEDED(hr)) hr = rd->LoadAsync((UINT32)size, &ld);
  if (SUCCEEDED(hr)) hr = AwaitOp(ld.Get());
  if (FAILED(hr)) return hr;
  uint8_t* b = (uint8_t*)malloc((size_t)size);
  if (!b) return E_OUTOFMEMORY;
  hr = rd->ReadBytes((UINT32)size, b);
  if (FAILED(hr)) { free(b); return hr; }
  *out = b;
  *outLen = (UINT32)size;
  return S_OK;
}

static HRESULT NewMemStream(ComPtr<wss::IRandomAccessStream>& ras) {
  ComPtr<IInspectable> insp;
  HRESULT hr = RoActivateInstance(
      HStringReference(RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream).Get(), &insp);
  if (SUCCEEDED(hr)) hr = insp.As(&ras);
  return hr;
}

// R: record MP4 (VideoRecord stream set to <=640x480, Vga encode, NO audio track) for g_secs, then count the frames.
// Evidence for plan C (a custom sink on the record stream gets the camera's raw rate without the encoder).
static int TestRecord(HSTRING camId) {
  ComPtr<wmc::IMediaCapture> mc;
  HRESULT hr = InitTelegArm(camId, mc);
  if (FAILED(hr) || !mc) { Result("NO-FRAMES MediaCapture init failed %s", HR(hr)); return 1; }
  AttachDiag(mc.Get());
  char recTxt[128] = "?";
  ComPtr<wmd::IMediaDeviceController> mdc;
  if (SUCCEEDED(GetMdc(mc.Get(), mdc))) {
    SetStream640(mdc.Get(), wmc::MediaStreamType_VideoRecord);
    ComPtr<wmp::IMediaEncodingProperties> cur;
    mdc->GetMediaStreamProperties(wmc::MediaStreamType_VideoRecord, &cur);
    snprintf(recTxt, sizeof recTxt, "%s", DescribeProps(cur.Get()));
    P("  VideoRecord stream: %s\n", recTxt);
  }
  STEP("CreateMp4(Vga)");
  ComPtr<wmp::IMediaEncodingProfileStatics> eps;
  hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Media_MediaProperties_MediaEncodingProfile).Get(),
                              IID_PPV_ARGS(&eps));
  ComPtr<wmp::IMediaEncodingProfile> prof;
  if (SUCCEEDED(hr)) hr = eps->CreateMp4(wmp::VideoEncodingQuality_Vga, &prof);
  if (SUCCEEDED(hr)) {
    prof->put_Audio(nullptr);  // video only: no dependency on the mic / capture mode
    ComPtr<wmp::IVideoEncodingProperties> vep;
    ComPtr<wmp::IMediaEncodingProperties> mep;
    if (SUCCEEDED(prof->get_Video(&vep)) && vep && SUCCEEDED(vep.As(&mep)))
      P("  MP4 profile video: %s (audio removed)\n", DescribeProps(mep.Get()));
  }
  ComPtr<wss::IRandomAccessStream> ras;
  if (SUCCEEDED(hr)) hr = NewMemStream(ras);
  if (FAILED(hr)) { Result("NO-FRAMES profile/stream setup failed %s", HR(hr)); CloseCapture(mc); return 1; }
  STEP("StartRecordToStreamAsync");
  ComPtr<wf::IAsyncAction> act;
  double t = NowMs();
  hr = mc->StartRecordToStreamAsync(prof.Get(), ras.Get(), &act);
  if (SUCCEEDED(hr)) hr = AwaitAction(act.Get());
  P("  StartRecordToStreamAsync(MP4 Vga, video only): %s (%.0f ms)\n", HR(hr), NowMs() - t);
  if (FAILED(hr)) {
    Result("NO-FRAMES StartRecordToStreamAsync failed %s record=[%s]", HR(hr), recTxt);
    CloseCapture(mc);
    return 1;
  }
  LogCamState(mc.Get(), "after StartRecord");
  STEP("recording");
  double r0 = NowMs();
  Sleep((DWORD)g_secs * 1000);
  double r1 = NowMs();
  STEP("StopRecordAsync");
  ComPtr<wf::IAsyncAction> stop;
  t = NowMs();
  hr = mc->StopRecordAsync(&stop);
  if (SUCCEEDED(hr)) hr = AwaitAction(stop.Get(), 15000);
  P("  StopRecordAsync: %s (%.0f ms)\n", HR(hr), NowMs() - t);
  STEP("read MP4");
  uint8_t* data = nullptr;
  UINT32 len = 0;
  HRESULT rh = ReadAll(ras.Get(), &data, &len);
  if (FAILED(rh) || !data) {
    Result("NO-FRAMES recorded 0 bytes (read %s, stop %s) record=[%s]", HR(rh), HR(hr), recTxt);
    CloseCapture(mc);
    return 1;
  }
  Mp4Video v = ParseMp4(data, len);
  free(data);
  uint32_t frames = v.samples ? v.samples : v.frag_samples;
  double fps = v.secs > 0 ? frames / v.secs : 0.0;
  double wallFps = (r1 > r0) ? frames * 1000.0 / (r1 - r0) : 0.0;
  P("  MP4: %u bytes, video track %s, %ux%u, %u frames, track duration %.2f s, recorded for %.0f ms\n", len,
    v.found ? "found" : "NOT found", v.w, v.h, frames, v.secs, r1 - r0);
  Result("%s fps=%.1f wall_fps=%.1f video_frames=%u duration=%.2fs bytes=%u mp4=%ux%u record=[%s]",
         Verdict(fps > 0 ? fps : wallFps), fps, wallFps, frames, v.secs, len, v.w, v.h, recTxt);
  CloseCapture(mc);
  return 0;
}

// P: the current backend's primitive (health check first, canary last)
static int TestPhoto(HSTRING camId) {
  ComPtr<wmc::IMediaCapture> mc;
  HRESULT hr = InitTelegArm(camId, mc);
  if (FAILED(hr) || !mc) { Result("NO-FRAMES MediaCapture init failed %s", HR(hr)); return 1; }
  AttachDiag(mc.Get());
  char photoTxt[128] = "?";
  ComPtr<wmd::IMediaDeviceController> mdc;
  if (SUCCEEDED(GetMdc(mc.Get(), mdc))) {
    ComPtr<wmp::IMediaEncodingProperties> cur;
    mdc->GetMediaStreamProperties(wmc::MediaStreamType_Photo, &cur);
    snprintf(photoTxt, sizeof photoTxt, "%s", DescribeProps(cur.Get()));
  }
  ComPtr<wmp::IImageEncodingPropertiesStatics> iep;
  hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_Media_MediaProperties_ImageEncodingProperties).Get(), IID_PPV_ARGS(&iep));
  ComPtr<wmp::IImageEncodingProperties> jpeg;
  if (SUCCEEDED(hr)) hr = iep->CreateJpeg(&jpeg);
  if (FAILED(hr)) { Result("CreateJpeg failed %s", HR(hr)); CloseCapture(mc); return 1; }
  int ok = 0;
  UINT64 bytes = 0;
  double t0 = 0;
  HRESULT firstHr = S_OK;
  for (int i = 0; i < 6; i++) {
    STEP("CapturePhotoToStreamAsync");
    if (i == 1) t0 = NowMs();  // the first call warms up; time the next 5
    ComPtr<wss::IRandomAccessStream> ras;
    hr = NewMemStream(ras);
    ComPtr<wf::IAsyncAction> act;
    if (SUCCEEDED(hr)) hr = mc->CapturePhotoToStreamAsync(jpeg.Get(), ras.Get(), &act);
    if (SUCCEEDED(hr)) hr = AwaitAction(act.Get());
    UINT64 sz = 0;
    if (SUCCEEDED(hr)) ras->get_Size(&sz);
    if (i == 0) { firstHr = hr; P("  first CapturePhotoToStreamAsync(JPEG): %s, %llu bytes\n", HR(hr), (unsigned long long)sz); }
    if (i > 0 && SUCCEEDED(hr) && sz) { ok++; bytes = sz; }
  }
  double dt = NowMs() - t0;
  double fps = (ok && dt > 0) ? ok * 1000.0 / dt : 0.0;
  Result("%s photo x5: %d ok in %.0f ms = %.1f fps (last %llu bytes, first call %s) photo=[%s]", Verdict(fps), ok, dt, fps,
         (unsigned long long)bytes, HR(firstHr), photoTxt);
  CloseCapture(mc);
  return 0;
}

// selftest: exercises the pixel/MP4 code without a camera (runs on the server too)
static void Put32(uint8_t* b, size_t* n, uint32_t v) {
  b[(*n)++] = (uint8_t)(v >> 24); b[(*n)++] = (uint8_t)(v >> 16); b[(*n)++] = (uint8_t)(v >> 8); b[(*n)++] = (uint8_t)v;
}
static size_t BoxOpen(uint8_t* b, size_t* n, const char* type) {
  size_t at = *n;
  Put32(b, n, 0);
  memcpy(b + *n, type, 4);
  *n += 4;
  return at;
}
static void BoxClose(uint8_t* b, size_t* n, size_t at) {
  size_t k = at;
  Put32(b, &k, (uint32_t)(*n - at));
}
static void Zeros(uint8_t* b, size_t* n, size_t k) { memset(b + *n, 0, k); *n += k; }
static void Trak(uint8_t* b, size_t* n, uint32_t id, const char* handler, uint32_t samples) {
  size_t trak = BoxOpen(b, n, "trak");
  size_t tkhd = BoxOpen(b, n, "tkhd");
  Put32(b, n, 0); Put32(b, n, 0); Put32(b, n, 0); Put32(b, n, id); Zeros(b, n, 4 + 4 + 8 + 8 + 36);
  Put32(b, n, 640u << 16); Put32(b, n, 480u << 16);
  BoxClose(b, n, tkhd);
  size_t mdia = BoxOpen(b, n, "mdia");
  size_t mdhd = BoxOpen(b, n, "mdhd");
  Put32(b, n, 0); Put32(b, n, 0); Put32(b, n, 0); Put32(b, n, 1000); Put32(b, n, 3000); Put32(b, n, 0);
  BoxClose(b, n, mdhd);
  size_t hdlr = BoxOpen(b, n, "hdlr");
  Put32(b, n, 0); Put32(b, n, 0); memcpy(b + *n, handler, 4); *n += 4; Zeros(b, n, 13);
  BoxClose(b, n, hdlr);
  size_t minf = BoxOpen(b, n, "minf");
  size_t stbl = BoxOpen(b, n, "stbl");
  size_t stsz = BoxOpen(b, n, "stsz");
  Put32(b, n, 0); Put32(b, n, 0); Put32(b, n, samples);
  BoxClose(b, n, stsz);
  BoxClose(b, n, stbl);
  BoxClose(b, n, minf);
  BoxClose(b, n, mdia);
  BoxClose(b, n, trak);
}

static int TestSelf() {
  int fails = 0;
  STEP("selftest SoftwareBitmap");
  ComPtr<wgi::ISoftwareBitmapFactory> sbf;
  HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Graphics_Imaging_SoftwareBitmap).Get(),
                                      IID_PPV_ARGS(&sbf));
  P("  SoftwareBitmap factory: %s\n", HR(hr));
  static const wgi::BitmapPixelFormat fmts[3] = {wgi::BitmapPixelFormat_Nv12, wgi::BitmapPixelFormat_Yuy2,
                                                 wgi::BitmapPixelFormat_Bgra8};
  static const int wantPlanes[3] = {2, 1, 1};
  static const UINT32 minCap[3] = {640 * 480 * 3 / 2, 640 * 480 * 2, 640 * 480 * 4};
  for (int i = 0; i < 3 && sbf; i++) {
    ComPtr<wgi::ISoftwareBitmap> sb;
    hr = sbf->Create(fmts[i], 640, 480, &sb);
    Stats st;
    memset(&st, 0, sizeof st);
    st.luma_min = -1;
    if (SUCCEEDED(hr) && sb) hr = ProcessBitmap(sb.Get(), &st, "selftest");
    bool good = SUCCEEDED(hr) && st.frames == 1 && st.planes == wantPlanes[i] && st.cap >= minCap[i] &&
                st.luma_first >= 0 && st.px_ok;
    P("  %s 640x480: %s planes=%d cap=%u luma=%d -> %s\n", PixName(fmts[i]), HR(hr), st.planes, st.cap, st.luma_first,
      good ? "ok" : "FAIL");
    if (!good) fails++;
    CloseIt(sb.Get());
  }
  if (!sbf) fails++;
  STEP("selftest VideoFrame + dedup");
  ComPtr<wm::IVideoFrameFactory> vff;
  hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Media_VideoFrame).Get(), IID_PPV_ARGS(&vff));
  ComPtr<wm::IVideoFrame> vf;
  if (SUCCEEDED(hr)) hr = vff->Create(wgi::BitmapPixelFormat_Bgra8, 320, 240, &vf);
  Stats vst;
  memset(&vst, 0, sizeof vst);
  vst.luma_min = -1;
  if (SUCCEEDED(hr) && vf) {
    ProcessVideoFrame(vf.Get(), &vst);  // same (blank, untimestamped) frame three times: 1 distinct, 2 repeats
    ProcessVideoFrame(vf.Get(), &vst);
    ProcessVideoFrame(vf.Get(), &vst);
  }
  bool vgood = SUCCEEDED(hr) && vst.frames == 3 && vst.w == 320 && vst.uniq == 1 && vst.same == 2 && vst.ts0 == 3;
  P("  VideoFrame.Create(Bgra8 320x240) x3 -> pixels=%d distinct=%d same=%d ts0=%d: %s -> %s\n", vst.frames, vst.uniq,
    vst.same, vst.ts0, HR(hr), vgood ? "ok" : "FAIL");
  if (!vgood) fails++;
  CloseIt(vf.Get());
  STEP("selftest MP4 parser");
  static uint8_t mp4[2048];
  size_t n = 0;
  size_t ftyp = BoxOpen(mp4, &n, "ftyp");
  memcpy(mp4 + n, "isom", 4); n += 4; Put32(mp4, &n, 0);
  BoxClose(mp4, &n, ftyp);
  size_t moov = BoxOpen(mp4, &n, "moov");
  Trak(mp4, &n, 1, "soun", 140);
  Trak(mp4, &n, 2, "vide", 90);
  BoxClose(mp4, &n, moov);
  Mp4Video v = ParseMp4(mp4, n);
  bool mgood = v.found && v.samples == 90 && v.secs > 2.99 && v.secs < 3.01 && v.w == 640 && v.h == 480 &&
               v.track_id == 2;
  Mp4Video tr = ParseMp4(mp4, n - 7);  // truncated input must not crash
  P("  MP4 parser: found=%d track=%u frames=%u secs=%.2f %ux%u -> %s (truncated input: found=%d)\n", (int)v.found,
    v.track_id, v.samples, v.secs, v.w, v.h, mgood ? "ok" : "FAIL", (int)tr.found);
  if (!mgood) fails++;
  Result("%s (%d failure(s))", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// driver: the parent runs each test in a child process (isolation + per-test watchdog + crash filter)

static const char* kTestsCam0[] = {"selftest",   "info",       "photo",        "fr-prev", "fr-rec",
                                   "prev",       "prev-640",   "fr-prev-640",  "fr-rec-640",
                                   "fr-prev-nv12", "fr-sg",    "fr-sg-640",    "fr-rec+prev",
                                   "prev-copy",  "record",     "photo"};
static const char* kTestsCam1[] = {"fr-prev", "prev-640"};
static HANDLE g_doneEvt;

static int RunTest(const char* test, int cam) {
  if (!strcmp(test, "selftest")) return TestSelf();
  Cam cams[8];
  int n = EnumCams(cams, 8, !strcmp(test, "info"));
  if (cam < 0 || cam >= n) { Result("no camera %d (WinRT sees %d)", cam, n); return 2; }
  HSTRING id = cams[cam].id.Get();
  P("  camera %d: %s [%s]\n", cam, cams[cam].name, PanelName(cams[cam].panel));
  FrOpts o;
  memset(&o, 0, sizeof o);
  o.want = wmc::MediaStreamType_VideoPreview;
  if (!strcmp(test, "info")) return TestInfo(id);
  if (!strcmp(test, "fr-prev")) return TestFrameReader(o, id, cam);
  if (!strcmp(test, "fr-prev-640")) { o.set640 = true; return TestFrameReader(o, id, cam); }
  if (!strcmp(test, "fr-prev-nv12")) { o.set640 = true; o.subtype = L"NV12"; return TestFrameReader(o, id, cam); }
  if (!strcmp(test, "fr-sg")) { o.sourceGroup = true; return TestFrameReader(o, id, cam); }
  if (!strcmp(test, "fr-sg-640")) { o.sourceGroup = true; o.set640 = true; return TestFrameReader(o, id, cam); }
  o.want = wmc::MediaStreamType_VideoRecord;
  if (!strcmp(test, "fr-rec")) return TestFrameReader(o, id, cam);
  if (!strcmp(test, "fr-rec-640")) { o.set640 = true; return TestFrameReader(o, id, cam); }
  if (!strcmp(test, "fr-rec+prev")) { o.withPreview = true; return TestFrameReader(o, id, cam); }
  if (!strcmp(test, "prev")) return TestPreview(id, false, false);
  if (!strcmp(test, "prev-640")) return TestPreview(id, true, false);
  if (!strcmp(test, "prev-copy")) return TestPreview(id, true, true);
  if (!strcmp(test, "record")) return TestRecord(id);
  if (!strcmp(test, "photo")) return TestPhoto(id);
  Result("unknown test");
  return 2;
}

// File-only reporting from the watchdog / crash filter: never block on the console, never wait on the log lock
// forever (the crash may have happened inside a log write).
static DWORD WINAPI Watchdog(void* arg) {
  DWORD ms = (DWORD)(UINT_PTR)arg;
  if (WaitForSingleObject(g_doneEvt, ms) == WAIT_TIMEOUT) {
    ResultFileOnly("HUNG in step '%s' for %lu s - watchdog killed the test", g_step, (unsigned long)(ms / 1000));
    TerminateProcess(GetCurrentProcess(), 3);
  }
  return 0;
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
  DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
  void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;
  HMODULE mod = NULL;
  wchar_t mn[MAX_PATH] = L"?";
  if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 (LPCWSTR)addr, &mod))
    GetModuleFileNameW(mod, mn, MAX_PATH);
  if (TryEnterCriticalSection(&g_logcs)) {
    if (g_out) {
      fprintf(g_out, "RESULT %-12s cam%d: CRASHED exception 0x%08lX at %p (module %ls base %p) in step '%s'\n", g_test,
              g_cam, (unsigned long)code, addr, mn, (void*)mod, g_step);
      fflush(g_out);
    }
    LeaveCriticalSection(&g_logcs);
  }
  TerminateProcess(GetCurrentProcess(), 4);
  return EXCEPTION_EXECUTE_HANDLER;
}

static wchar_t g_logPath[MAX_PATH + 64];

static void RunChild(const char* test, int cam) {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  wchar_t cmd[4096];
  swprintf(cmd, 4096, L"\"%ls\" --child %hs --cam %d --secs %d --log \"%ls\"", exe, test, cam, g_secs, g_logPath);
  STARTUPINFOW si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  snprintf(g_test, sizeof g_test, "%s", test);  // for the parent's own RESULT lines about this child
  g_cam = cam;
  double t = NowMs();
  if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    Result("NOT RUN - CreateProcess: %s", HR(HRESULT_FROM_WIN32(GetLastError())));
    snprintf(g_test, sizeof g_test, "main");
    return;
  }
  DWORD limit = (DWORD)(g_secs + 150) * 1000;
  if (WaitForSingleObject(pi.hProcess, limit) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 9);
    WaitForSingleObject(pi.hProcess, 5000);
    Result("HUNG - parent killed it after %lu s", (unsigned long)(limit / 1000));
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code >= 0xC0000000u) Result("CRASHED - child exit code 0x%08lX (no handler ran)", (unsigned long)code);
  P("  [%s cam%d: exit %lu, %.1f s]\n", test, cam, (unsigned long)code, (NowMs() - t) / 1000.0);
  snprintf(g_test, sizeof g_test, "main");
}

static void Summary() {
  EnterCriticalSection(&g_logcs);
  if (g_out) fflush(g_out);
  LeaveCriticalSection(&g_logcs);
  FILE* f = _wfopen(g_logPath, L"rb");
  if (!f) { P("\n(summary: could not reopen the log)\n"); return; }
  const size_t kMax = 256 * 1024;
  char* acc = (char*)malloc(kMax);
  size_t used = 0;
  char line[2048];
  while (acc && fgets(line, sizeof line, f)) {
    if (strncmp(line, "RESULT", 6)) continue;
    size_t k = strlen(line);
    if (used + k + 1 < kMax) { memcpy(acc + used, line, k); used += k; }
  }
  fclose(f);
  if (!acc) return;
  acc[used] = 0;
  P("\n== SUMMARY ==\n");
  double best = 0;
  char bestLine[400] = "";
  for (char* ln = acc; *ln;) {
    char* nl = strchr(ln, '\n');
    if (nl) *nl = 0;
    P("%s\n", ln);
    char name[32] = "";
    int c = -1;
    const char* sc = strstr(ln, "SCORE=");
    if (sc && sscanf(ln, "RESULT %31s cam%d:", name, &c) == 2 && (!strncmp(name, "fr-", 3) || !strncmp(name, "prev", 4))) {
      double s = atof(sc + 6);
      if (s > best) { best = s; snprintf(bestLine, sizeof bestLine, "%s on cam%d at %.1f fps", name, c, s); }
    }
    if (!nl) break;
    ln = nl + 1;
  }
  if (best >= 10.0) P("WINNER: %s - port this primitive.\n", bestLine);
  else if (best > 0) P("BEST (below 10 fps): %s\n", bestLine);
  else P("WINNER: none - no streaming test delivered readable live frames.\n");
  P("  legend: fr-* = MediaFrameReader (prev = VideoPreview source, rec = VideoRecord source, -640 = SetFormatAsync to\n"
    "          <=640x480, -nv12 = NV12 output, sg = SourceGroup init, rec+prev = record reader while a preview runs);\n"
    "          prev* = StartPreviewAsync + GetPreviewFrameAsync (-640 = 640x480 preview, -copy = Bgra8 copy);\n"
    "          record = 4 s MP4 (plan C evidence); photo = current backend (first = health check, last = canary).\n"
    "  verdict: STREAMS >= 10 sustained distinct fps | SLOW | NO-FRAMES (nothing arrived); (no-CPU-pixels) = frames\n"
    "          came but their bytes weren't readable; (FLAT-image?) = black/constant frames. SCORE ranks the winner.\n"
    "  Send this whole file back.\n");
  free(acc);
}

static void ParseList(char* list, const char** out, int* n, int max) {
  *n = 0;
  for (char* tok = strtok(list, ","); tok && *n < max; tok = strtok(NULL, ",")) out[(*n)++] = tok;
}

static FILE* OpenLog(const wchar_t* path) {
  FILE* f = _wfopen(path, L"ab");
  if (f) {
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0) { static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF}; fwrite(bom, 1, 3, f); }
  }
  return f;
}

int wmain(int argc, wchar_t** argv) {
  QueryPerformanceFrequency(&g_qpf);
  InitializeCriticalSection(&g_logcs);
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);  // no WER dialog (inherited)
  g_mferr = LoadLibraryExW(L"mferror.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  char child[32] = "";
  char tests[512] = "";
  int cam = 0;
  bool camGiven = false, pause = true;
  const wchar_t* logArg = NULL;
  const wchar_t* dirArg = NULL;
  for (int i = 1; i < argc; i++) {
    if (!wcscmp(argv[i], L"--child") && i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, child, sizeof child, NULL, NULL);
    else if (!wcscmp(argv[i], L"--tests") && i + 1 < argc) WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, tests, sizeof tests, NULL, NULL);
    else if (!wcscmp(argv[i], L"--cam") && i + 1 < argc) { cam = _wtoi(argv[++i]); if (cam < 0) cam = 0; camGiven = true; }
    else if (!wcscmp(argv[i], L"--secs") && i + 1 < argc) { g_secs = _wtoi(argv[++i]); if (g_secs < 1) g_secs = 1; if (g_secs > 30) g_secs = 30; }
    else if (!wcscmp(argv[i], L"--log") && i + 1 < argc) logArg = argv[++i];
    else if (!wcscmp(argv[i], L"--nopause")) pause = false;
    else if (argv[i][0] != L'-') dirArg = argv[i];
  }
  if (logArg) {
    swprintf(g_logPath, MAX_PATH + 64, L"%ls", logArg);
  } else {
    wchar_t dir[MAX_PATH];
    if (dirArg) swprintf(dir, MAX_PATH, L"%ls", dirArg);
    else { GetModuleFileNameW(NULL, dir, MAX_PATH); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
    size_t dl = wcslen(dir);
    if (dl && (dir[dl - 1] == L'\\' || dir[dl - 1] == L'/')) dir[dl - 1] = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf(g_logPath, MAX_PATH + 64, L"%ls\\winrtcamprobe-%04d%02d%02d-%02d%02d%02d.txt", dir, st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
  }
  g_out = OpenLog(g_logPath);

  if (child[0]) {  // one test, in this process
    snprintf(g_test, sizeof g_test, "%s", child);
    g_cam = cam;
    SetUnhandledExceptionFilter(CrashFilter);
    P("\n===== TEST %s  (camera %d, %d s window) =====\n", child, cam, g_secs);
    g_doneEvt = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE wd = CreateThread(NULL, 0, Watchdog, (void*)(UINT_PTR)((g_secs + 90) * 1000), 0, NULL);
    int rc;
    {
      RoInitializeWrapper ro(RO_INIT_MULTITHREADED);
      g_t0 = NowMs();
      if (FAILED((HRESULT)ro)) { Result("RoInitialize failed %s", HR((HRESULT)ro)); rc = 1; }
      else rc = RunTest(child, cam);
    }
    SetEvent(g_doneEvt);
    if (wd) { WaitForSingleObject(wd, 2000); CloseHandle(wd); }
    CloseLog();
    return rc;
  }

  // parent: no QuickEdit (a tap/click on the console would freeze every child's output)
  HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD conMode = 0;
  bool haveMode = GetConsoleMode(hin, &conMode) != 0;
  if (haveMode) SetConsoleMode(hin, (conMode | ENABLE_EXTENDED_FLAGS) & ~(DWORD)ENABLE_QUICK_EDIT_MODE);
  if (!g_out) {  // e.g. a read-only folder: fall back to %TEMP%
    wchar_t tmp[MAX_PATH];
    DWORD tl = GetTempPathW(MAX_PATH, tmp);
    if (tl && tl < MAX_PATH) {
      const wchar_t* base = wcsrchr(g_logPath, L'\\');
      swprintf(g_logPath, MAX_PATH + 64, L"%ls%ls", tmp, base ? base + 1 : L"winrtcamprobe.txt");
      g_out = OpenLog(g_logPath);
    }
  }
  P("winrtcamprobe %s (Venetium) - which WinRT camera primitive streams, and how fast\n", PROBE_VERSION);
  {
    RTL_OSVERSIONINFOW v;
    v.dwOSVersionInfoSize = sizeof v;
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    LONG(WINAPI * rtl)(PRTL_OSVERSIONINFOW) =
        nt ? (LONG(WINAPI*)(PRTL_OSVERSIONINFOW))(void*)GetProcAddress(nt, "RtlGetVersion") : NULL;
    if (rtl && rtl(&v) == 0) P("windows %lu.%lu.%lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
  }
  { char p8[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, g_logPath, -1, p8, sizeof p8, NULL, NULL); P("log: %s%s\n", p8, g_out ? "" : "  (!! COULD NOT OPEN - console only)"); }
  int ncams = 0;
  {
    RoInitializeWrapper ro(RO_INIT_MULTITHREADED);
    P("RoInitialize: %s\n", HR((HRESULT)ro));
    Cam cams[8];
    ncams = EnumCams(cams, 8, true);
    P("WinRT cameras: %d\n", ncams);
  }
  const char* list[64];
  int nlist = 0;
  int cams[64];
  if (tests[0]) {
    ParseList(tests, list, &nlist, 64);
    for (int i = 0; i < nlist; i++) cams[i] = cam;
  } else {
    int c0 = camGiven ? cam : 0;
    for (size_t i = 0; i < sizeof kTestsCam0 / sizeof kTestsCam0[0] && nlist < 64; i++) { list[nlist] = kTestsCam0[i]; cams[nlist++] = c0; }
    if (!camGiven && ncams >= 2)
      for (size_t i = 0; i < sizeof kTestsCam1 / sizeof kTestsCam1[0] && nlist < 64; i++) { list[nlist] = kTestsCam1[i]; cams[nlist++] = 1; }
  }
  for (int i = 0; i < nlist; i++) {
    if (i > 0 && ncams > 0 && strcmp(list[i], "selftest")) Sleep(1500);  // let the driver release the camera
    RunChild(list[i], cams[i]);
  }
  Summary();
  { char p8[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, g_logPath, -1, p8, sizeof p8, NULL, NULL); P("\n== done ==  log: %s\n", p8); }
  CloseLog();
  if (haveMode) SetConsoleMode(hin, conMode);
  if (pause) { printf("\nPress Enter to close..."); fflush(stdout); getchar(); }
  return 0;
}
