// Chromium replay table - from two independent source derivations (F:\cr\sbxprobe\spec\spec-a.json, spec-b.json; snapshot of
// F:\cr\src at state-0028), which agree on every value below. Build 15035 maps to base::win::Version::WIN10_RS1, so:
// mitigation word size 8, value2 = 0, JOB_LIST + CHILD_PROCESS_POLICY on (>= WIN10 / TH2), COMPONENT_FILTER (KTM) always,
// HANDLE_LIST always (Mojo endpoint + shared memory), creation flags 0x0008040C, STARTF_FORCEOFFFEEDBACK|STARTF_USESTDHANDLES.
// requested_v1 is the word BEFORE Chromium masks it with ProcessMitigationOptionsMask; the probe masks it the same way.
//   HEAP_TERMINATE 1<<12 | BOTTOM_UP_ASLR 1<<16 | EXTENSION_POINT_DISABLE 1<<32 | FONT_DISABLE 1<<48 |
//   IMAGE_LOAD_NO_REMOTE 1<<52 | IMAGE_LOAD_NO_LOW_LABEL 1<<56            = 0x0111000100011000 (GPU)
//   + WIN32K_SYSTEM_CALL_DISABLE 1<<28                                    = 0x0111000110011000 (renderer, storage service)
//   + BLOCK_NON_MICROSOFT_BINARIES 1<<44 (renderer with code integrity)    = 0x0111100110011000
// (DEP / SEHOP are excluded on ARM32 by process_mitigations.cc; RS2+ / RS3+ / 20H1+ flags are off on the RS1 mapping.)
// Desktop: GPU uses the default desktop; renderer and services Chromium's alternate window station + desktop (desk 2; the
// probe makes one the same way, CreateWindowStationW + CreateDesktopW). Environment: renderer and services get the filtered
// block (Path, SystemDrive, SystemRoot, TEMP, TMP, LOCALAPPDATA, CHROME_CRASHPAD_PIPE_NAME). Tokens: GPU USER_LIMITED,
// renderer/services USER_LOCKDOWN, both low IL. Extra handles: utility = + bootstrap event, renderer with code integrity =
// + two SEC_IMAGE sections (chrome.dll, chrome_elf.dll).
// args != NULL: the row is also run as the real-image control (venetium.exe with these arguments, suspended, never run,
// Chromium-like HANDLE_LIST).
static const Replay kReplay[] = {
    // type, args, requested_v1, v2, child, job, handles, ktm, token, desk, env, extra
    {"gpu", L"--type=gpu-process", 0x0111000100011000ull, 0, 1, 1, 1, 1, 6, 0, 0, 0},
    {"renderer", L"--type=renderer", 0x0111000110011000ull, 0, 1, 2, 1, 1, 5, 2, 1, 0},
    {"renderer+code-integrity", L"--type=renderer", 0x0111100110011000ull, 0, 1, 2, 1, 1, 5, 2, 1, 2},
    {"utility-storage",
     L"--type=utility --utility-sub-type=storage.mojom.StorageService --service-sandbox-type=service",
     0x0111000110011000ull, 0, 1, 2, 1, 1, 5, 2, 1, 1},
    // the GPU and renderer words again with simpler tokens / no desktop / no environment, so a token, desktop or
    // environment problem cannot hide an attribute problem
    {"gpu (own token duplicate)", NULL, 0x0111000100011000ull, 0, 1, 1, 1, 1, 1, 0, 0, 0},
    {"gpu (no token, CreateProcessW)", NULL, 0x0111000100011000ull, 0, 1, 1, 1, 1, 0, 0, 0, 0},
    {"renderer (no token, default desktop)", NULL, 0x0111000110011000ull, 0, 1, 2, 1, 1, 0, 0, 0, 0},
};
