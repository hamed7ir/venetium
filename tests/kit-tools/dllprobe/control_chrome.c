// BATCH-SBX-1 server control: a tiny stand-in "chrome.dll" (x64) with one dependency (dllprobe_dep.dll). There is no x64 Venetium
// chrome.dll, so this stands in to validate the probe's load/read-back/snaps mechanism on the server. The device loads the real
// ARM32 chrome.dll. Not shipped.
__declspec(dllimport) int dllprobe_dep_fn(void);
__declspec(dllexport) int chrome_entry(void) { return dllprobe_dep_fn(); }
