// BATCH-SBX-1 server control: the dependency DLL the stand-in chrome.dll imports, so removing it makes the loader report a
// missing module (the H2 demonstration on x64). Not shipped; server instrument check only.
__declspec(dllexport) int dllprobe_dep_fn(void) { return 0x5B58; }
