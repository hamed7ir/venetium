# kit-tools/webrtc

Server-side tools for the kit page `webrtc`. Nothing here goes to the device and the page never loads it.

`kit/webrtc.html` has no test files: no image, audio, video or data file is read. The camera picture and the microphone signal come
from the browser itself: the runner passes `--use-fake-device-for-media-stream --use-fake-ui-for-media-stream` (a synthetic green test
pattern with a frame counter and a periodic beep); on the device they come from the real camera and microphone, behind a permission
prompt. So `kit/manifest/webrtc.tsv` lists only the page itself (the explanation that used to be a comment line in that file lives here;
a manifest holds a header row and rows only).

## mutants.js

Mutation checks for the page: copies the page, `kit/lib` and the runner to a scratch folder, applies ONE mutation, and runs the runner
against the copy (`--run`), or prints the command. `node mutants.js <scratch-dir> <mutation> [--run]` with
`F:\cr\src\third_party\node\win\node.exe`. Each mutation prints what the page must then do:

- `late-gum`, `late-gum-nostop`: a getUserMedia that answers after its time limit has its tracks stopped (and the check fails when they are not)
- `no-frame`, `no-frame-noguard`: the colour check needs a frame in both `<video>` elements (and passes on two black elements without the guard)
- `autoplay-refused`: the default autoplay policy refuses unmuted playback: the page mutes the remote element and still passes, with
  the one audible-only check reported n/a; `autoplay-refused-nofallback` shows the page fails without that fallback

The page EXERCISES `rnn_fc.cc` (Venetium 0013: AGC2's RNN voice-activity detector runs when `autoGainControl` is on) and does NOT VERIFY it.

Follow-up round (BATCH-DEVICE-1 follow-up, group webrtc-canvas): the check "inbound audio carries the signal (inbound energy > 0)" is asserted
only when the loudest source `audioLevel` is at least 0.003 (about 100 LSB of int16; about 10 LSB after the 0.1 playout gain, which WebRTC applies
to the samples before it measures the inbound level). Below that it is n/a with the reason "source level <x> too low to survive the 0.1 playout
gain" (a quiet room microphone reaches the receiver as all-zero samples on a healthy path). Energies and levels are printed in exponent form.
`mutants.js` writes its runs to `F:\cr\device-1\runs\mut2-webrtc-<mutation>` now and has four more entries:

- `quiet-rms1` ... `quiet-rms300` (1, 3, 5, 10, 30, 60, 100, 300): not mutations; the page is unchanged and the fake microphone is fed a WAV of
  Gaussian white noise at that rms in LSB (made by the tool: 48 kHz, mono, 16 bit, 12 s) through `--use-file-for-fake-audio-capture`. The very
  quiet ones (up to 30) give PASS with exactly one n/a; from 60 on the source level is >= 0.003 and the check is real and ok.
- `inbound-energy-zero`: the stats the page reads say the inbound audio energy and level are 0 while the (default, loud) fake microphone is loud;
  the signal check must be BAD, exactly one bad check.
- `no-audio-playout`: the remote audio track is never given to the playing element (a real broken receive path: WebRTC pulls no samples); the
  signal check must be BAD (the loud source keeps it from being n/a).
