Venetium GPU probe v2
=====================

This collects what we need to turn on hardware acceleration (GPU) in Venetium on
your device. It only reads information and draws small test images off-screen.
It changes nothing on your tablet and installs nothing.

What to do:

1. Extract the whole zip to a folder, for example C:\gpuprobe.

2. Double-click run-probe.cmd and wait until it says "Step 1 done"
   (one to three minutes).

3. Double-click render-check.cmd (normally, NOT "Run as administrator").
   It opens Venetium four times, each time with different GPU settings and a
   fresh test profile, so your own Venetium profile is not touched. Each time:
     - look at the WebGL fish tank tab (smooth, slow, black or garbled?),
     - on the chrome://gpu tab click "Copy Report to Clipboard",
     - close Venetium,
     - type what you saw and press Enter.
   The fish tank page needs an internet connection.

4. Zip the "results" folder and send it to us.

If something crashes or hangs, that is useful information too: the probe notes
it and goes on. If Venetium is not found, the script asks you to drag
venetium.exe onto its window.

Files:
  gpuprobe.exe       the probe (Windows ARM32, Windows RT 8.0 / 8.1 / 10)
  run-probe.cmd      step 1
  render-check.cmd   step 2
  results\           everything that is saved (send this)
