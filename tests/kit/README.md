# Venetium on the Surface 2 — the device trip

You carry one file: **`venetium-150.0.7871.226-win-arm32.zip`**. It holds the browser and this test kit (`kit\`).
Do the steps in order. Write things down where it says **Write down**. Where it says **Stop**, stop and send what you have.

## A. Get it onto the Surface 2
1. Copy the zip to the Surface 2 — whichever way is easiest: Remote Desktop with your drive shared, a USB stick, or a download.
2. Right-click the zip → **Extract All…** → choose a folder (for example `C:\Venetium`; a folder name with spaces is fine).
3. Open the extracted folder. It holds `venetium.exe`, `chrome.dll`, many other files, and the `kit` folder.

## A2. This trip first: crash dumps and the sandbox probe (ARM-FIX-2)
This build (Venetium 0029) starts its sandboxed processes without the setting Windows 15035 ARM32 refused, so B below is the real
test. A second, separate crash (exit code 0xC0000409) leaves no dump on its own, so turn on Windows' own crash dumps first. Every
change here is undone at the end (A2-7). The same steps, in a file Notepad shows properly on the Surface 2:
`kit\diag\README.txt`.

A2-1. Open an **administrator** command prompt: Start → type `cmd` → right-click **Command Prompt** (on the touch screen: press
      and hold) → **Run as administrator**. Type each line below and press **Enter**. "The operation completed successfully." is
      right; "Access is denied" means the prompt is not an administrator one.
A2-2. First, see whether crash dumps were ever set up on this computer:
```
reg query "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps"
```
      **Write down** whether it says "ERROR: The system was unable to find the specified registry key or value." (A2-7 needs it.)
A2-3. Crash dumps **ON** for Venetium (two lines; the second keeps up to 30 dumps instead of 10):
```
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /v DumpType /t REG_DWORD /d 2 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /v DumpCount /t REG_DWORD /d 30 /f
```
A2-4. Double-click **`kit\diag\run-probe.cmd`** (the sandbox probe, version 3.1; a few seconds to a few minutes). Do not tap or
      click inside its black window while it runs; if it seems frozen, press Esc once. Wait for "Press any key". It writes
      `kit\diag\sbxprobe-<date>-<time>.log` and changes nothing on the computer.
A2-5. Now do B (double-click `venetium.exe`). If Venetium closes by itself, wait about a minute, then type
      `%LOCALAPPDATA%\CrashDumps` into the File Explorer address bar: Windows should have written `venetium.exe.<number>.dmp`
      there. Dumps are expected for the main Venetium process (the window that closes); the GPU and sandboxed helper processes
      will probably not leave one. "No .dmp appeared" is also a result — write it down. A dump holds whatever the browser had
      open, so use only the kit's pages, and each dump can be a few hundred MB.
A2-6. The four logged runs, as last time. Double-click `kit\diag\log-run.cmd` (plain). Then open a command prompt in `kit\diag`
      (open that folder in File Explorer, click the address bar, type `cmd`, press Enter) and type, one at a time:
      `.\log-run.cmd --disable-gpu`, `.\log-run.cmd --no-sandbox`, `.\log-run.cmd --no-sandbox --disable-gpu`.
      Do A2-6 even if B tells you to stop. Then carry on with C, D and E — leave the crash dumps on until the end.
A2-7. At the very end (after E): send the `.dmp` files from `%LOCALAPPDATA%\CrashDumps` and the `kit\diag` folder zipped
      (right-click → Send to → Compressed (zipped) folder) with the rest. Then turn the crash dumps **OFF** in an administrator
      command prompt — if A2-2 said "unable to find", type the first line; otherwise the second:
```
reg delete "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /f
reg delete "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\venetium.exe" /f
```

## A3. This trip: GPU, video and the 720p question (BATCH-GPU-1)
This build changes how Venetium draws and plays video on the Surface 2. These steps measure it without the network getting in the
way, and read what the tablet's own graphics driver can do. None of them change anything on the computer. Do them after A2's crash
dumps are on, and before B. The pages play short local clips on a loop and run an animation for a minute — that is expected.

A3-1. **Plug in the charger** and leave it plugged in for all of A3.
A3-2. Double-click **`kit\diag\run-d3dprobe.cmd`**. It takes a few seconds and prints **done**. It writes
      `kit\diag\d3dprobe-<date>-<time>.txt` (what the Direct3D driver supports). Wait for "Press any key".
A3-3. Double-click **`kit\gpu\gpu-ab.cmd`**. It runs five graphics configurations one after another, each for up to five minutes,
      and **closes every Venetium window between them** — so **do not open or touch Venetium (or this window) until it prints
      "all done"**, about 25 minutes. It writes a chrome log for each configuration under `kit\gpu\logs\`.
A3-4. Now start Venetium the normal way (double-click `venetium.exe`). Open a **YouTube** video, set it to **720p** (gear →
      Quality → 720p), right-click the video → **Stats for nerds**, and let it play for **3 minutes**. Take a **screenshot**
      (press the Windows key + PrtScn; it saves into Pictures\Screenshots). Then browse your usual sites for a few minutes.
      **Write down** every freeze — roughly how long each one lasted and what was on screen.
A3-5. In that same Venetium window, open **`chrome://gpu`**, press **Ctrl+S**, and save it (Webpage, Single File if offered) into
      the **`kit\gpu`** folder.
A3-6. Zip the **`kit\gpu`** folder and the **`kit\diag`** folder (right-click → Send to → Compressed (zipped) folder) and send
      them, with the YouTube screenshot and your notes.

## A4. This trip: why the sandbox can't open pages (BATCH-SBX-1)
With the sandbox on, the window opens but every page is dead; with `--no-sandbox` it works. This one step measures *why*, so the
fix can keep the sandbox on. It changes nothing, needs no admin, and nothing to undo. Do it any time during the trip.

A4-1. Double-click **`kit\diag\run-dllprobe.cmd`**. It takes a few seconds and prints **done**. Wait for "Press any key".
A4-2. It writes **`kit\diag\dllprobe-<date>-<time>.txt`** (it reads your `chrome.dll` under the sandbox token shapes and reports
      what each can read/find). It changes nothing on the computer.
A4-3. That file is inside `kit\diag`, so A3-6's `kit\diag` zip already carries it — just make sure you ran this before zipping.

## A5. This trip: smooth 720p — hardware H.264, GPU drawing, GPU colour for VP9/AV1 (BATCH-DEVICE-7 + BATCH-MEDIA-1)
This build uses the tablet's own video engine for H.264, draws web pages on the GPU, and does the colour conversion of VP9/AV1
video on the GPU instead of the processor. It also has the DEVICE-7 fixes: stronger shaders, Direct3D 9 only, and a sandbox fix
so pages should now open **with** the sandbox on. None of these steps change anything on the computer. Do them before B.

A5-1. **Plug in the charger** and leave it plugged in for all of A5.
A5-2. Double-click **`venetium.exe`** (no switches — the sandbox is on). Open a normal site (any news page). **Write down:** does
      the page open and show its text and pictures? (With the last build pages stayed blank unless `--no-sandbox` was used.) If
      the page is blank, write that down and carry on — the last run of A5-3 uses `--no-sandbox`. Close Venetium.
A5-3. Double-click **`kit\media1\media1-ab.cmd`**. It runs five configurations one after another (about 18 minutes) and **closes
      every Venetium window between them** — do not open or touch Venetium until it prints **"all done"**. It writes a chrome
      log for each configuration under `kit\media1\logs\`.
A5-4. Start `venetium.exe` the normal way. Open **`chrome://gpu`**, press **Ctrl+S** and save it into **`kit\media1`**.
      **Write down** what the lines **Rasterization** and **Video Decode** say.
A5-5. Drag **`kit\video\video.html`** into the window. While its first clip (720p H.264) plays, open a new tab with
      **`chrome://media-internals`**, click the playing entry, and **write down** `kVideoDecoderName` and
      `kIsPlatformVideoDecoder`. Hardware H.264 shows **`VDAVideoDecoder`** and **`true`**; `FFmpegVideoDecoder` means software.
A5-6. **YouTube:** open a video, set it to **720p**, right-click the video → **Stats for nerds**, and let it play **3 minutes**.
      Take a **screenshot** (Windows key + PrtScn). **Write down** the codec in the stats (`vp09`, `av01` or `avc1`) and the
      **dropped frames**. In `chrome://media-internals` write down its `kVideoDecoderName` too (`VpxVideoDecoder` = VP9,
      `Dav1dVideoDecoder` = AV1, both decoded by the processor — this chip has no VP9/AV1 video engine; `VDAVideoDecoder` = H.264
      on the video engine).
A5-7. Only if A5-6 showed **`av01`** and it stuttered: close Venetium, open a command window in the Venetium folder
      (File Explorer → File → Open Windows PowerShell), and start it with **`.\venetium.exe --enable-features=VenetiumNoAV1`**.
      Repeat A5-6 with the same video and **write down** the codec (it should now be `vp09`) and the dropped frames.
A5-8. **Aparat:** open a video, set it to **720p**, play **3 minutes**. **Write down** how smooth it is and its
      `kVideoDecoderName` from `chrome://media-internals` (expected `VDAVideoDecoder`).
A5-9. Zip the **`kit\media1`** folder (right-click → Send to → Compressed (zipped) folder) and send it with the screenshots and
      your notes.

## B. First launch (the gate)
4. Double-click **`venetium.exe`**.
5. **Write down:** did a window open? What did the first page show? Any message from Windows?
6. **Stop** if no window opens, Windows refuses to run it, or it crashes. Send the exact message (a photo is fine).
   Nothing below matters until it launches.
7. Close Venetium.

## C. The test kit (one run per configuration)
8. Open `kit\launchers`.
9. Double-click **`default.cmd`**. Venetium opens the kit page and runs every test by itself (a few minutes; do not click inside
   the page while it runs).
10. When the camera and microphone test asks for permission, click **Allow**, then talk or clap for about 10 seconds (it checks
    that the microphone's sound gets through).
11. Wait until the big word at the top says **PASS** or **FAIL**.
12. Click **Save results**. Venetium saves `kit-results-default-<time>.json` to your **Downloads** folder.
13. **Write down:** the big word (PASS / FAIL). If FAIL, take a photo of the table (it names the failed pages).
14. Close Venetium.
15. Do steps 9–14 for each of these launchers, one at a time: **`jitless.cmd`**, **`ignition.cmd`**, **`sparkplug.cmd`**,
    **`maglev.cmd`**, **`turbofan.cmd`**, **`maglev-caller.cmd`**, **`turbofan-caller.cmd`**, **`liftoff-only.cmd`**,
    **`no-liftoff.cmd`**.
16. **Write down** the two Maglev runs on their own: `maglev.cmd` = ____, `maglev-caller.cmd` = ____, and in each, what the
    **tier proof** column says for `js-f1` and `js-jit`. On the Surface 2 both runs must say **PASS** and the tier proof must
    say **proven maglev** (for example "proven maglev (callees, 6 functions)") — not "n/a". There they prove V8's Maglev tier,
    which the x86 control build does not have.
17. **Stop** at the first launcher that crashes, or that shows no PASS/FAIL after 20 minutes. Send what you have. A page
    whose frame goes blank or shows a "crashed" / "Aw, Snap" message is itself a finding — take a photo of it.

## D. Checks only the Surface 2 can do
18. **Graphics:** open a new tab, type `chrome://gpu` and press Enter. Click **Copy Report to Clipboard**, paste it into Notepad
    and save it as `gpu.txt`. **Write down:** do "Canvas" and "WebGL" say *Hardware accelerated*? Which line names the GPU (look
    for `ANGLE`, `Direct3D9`, `NVIDIA`, `Tegra`)? The aim is Direct3D 9 on the Tegra (feature level 9_1).
19. **Video:**
    - Open `kit\media.html` (drag it into a Venetium window). It plays short clips, including H.264. **Write down:** its PASS /
      FAIL, and whether the pictures move.
    - Open a normal web video (any news or video site). **Write down:** does it play, and is it smooth?
    - While a video plays, open a new tab with `chrome://media-internals`, click the playing entry and find **`kVideoDecoderName`**.
      **Write down** its value (`FFmpegVideoDecoder` = software; `D3D11VideoDecoder` or a `DXVA` name = hardware).
20. **Look and identity:**
    - Taskbar, Start menu and File Explorer: does the Venetium logo show? Check at 100 % and at 200 % scaling (Settings → System →
      Display → Scale). **Write down** anything blurry or wrong.
    - Right-click `venetium.exe` → **Properties** → **Details**. **Write down:** File description, Product name, Copyright, Product
      version (expected: Venetium · Venetium · Copyright 2026 The Chromium Authors… · 150.0.7871.226).
    - The **⋮** menu → **Help** → **About Venetium**: **write down** the name and version it shows.
    - Open `kit\fonts.html`. **Write down:** do all its lines show real letters, or do some show empty boxes (□)?
21. **Live Caption:** not available on ARM32 (Google ships no ARM32 speech engine) — nothing to test.

## E. What to send back
22. All the `kit-results-*.json` files from **Downloads**, `gpu.txt`, the **`kit\media1`** zip from A5, your written notes, and
    photos of anything that failed.
23. If something crashed or would not open, send that first — do not push past it.

## Notes
- Each launcher uses its own test profile (`kit-profile-<name>`, created next to the `kit` folder), never your own.
- Some kit pages show a "CPU architecture" row. On the Surface 2 it may read *architecture "" · bitness 64*: that is how Chromium
  reports Windows ARM32, not a problem.
- `kit\lib\build.js` tells the kit which build it belongs to (ARM32, Maglev present). Leave it as it is.
- If a launcher says "venetium.exe not found", the `kit` folder is not inside the folder that holds `venetium.exe`. Keep the
  extracted folder as it is, or open a command prompt in `kit\launchers`, type `set VENETIUM=C:\path\to\venetium.exe`, and run the
  launcher from there.
- Extra browser arguments go after a launcher's name, e.g. `default.cmd --lang=de`.
- `index.html?armed=1` is the kit's self-test (every page must then FAIL on purpose). You do not need it on the trip.
- `liftoff-only.cmd` takes a little longer than the others on purpose.
- Grey "n/a" and "(info)" rows never fail a page; only red rows do.
