// Venetium test kit — media.html's PCM WAV test file, built at run time (BATCH-X86-2 §1). None of Chromium's own WAV test files is longer
// than 1.07 s, and the page must play a clip past 2 s, so the pcm clip is generated: a RIFF/WAVE header + PCM s16le samples.
//   KitWav.make({seconds, rate, freq, amp}) -> {bytes: Uint8Array, rate, channels, seconds, amp}
// The samples are a pure sine (amp * sin(2*pi*freq*n/rate)), so a decoder's output can be checked exactly (peak, RMS, length).
window.KitWav = {
  make(o) {
    o = Object.assign({ seconds: 3.0, rate: 22050, freq: 440, amp: 0.3 }, o || {});
    const n = Math.round(o.seconds * o.rate), dataBytes = n * 2, u = new Uint8Array(44 + dataBytes), v = new DataView(u.buffer);
    const tag = (at, s) => { for (let i = 0; i < 4; i++) u[at + i] = s.charCodeAt(i); };
    tag(0, 'RIFF'); v.setUint32(4, 36 + dataBytes, true); tag(8, 'WAVE'); tag(12, 'fmt ');
    v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);   // PCM, mono
    v.setUint32(24, o.rate, true); v.setUint32(28, o.rate * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
    tag(36, 'data'); v.setUint32(40, dataBytes, true);
    for (let i = 0; i < n; i++) v.setInt16(44 + 2 * i, Math.round(o.amp * 32767 * Math.sin(2 * Math.PI * o.freq * i / o.rate)), true);
    return { bytes: u, rate: o.rate, channels: 1, seconds: n / o.rate, amp: o.amp, freq: o.freq };
  },
};
