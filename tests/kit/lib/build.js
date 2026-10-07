// Venetium test kit — the build this kit copy is for (BATCH-DEVICE-1). null = unknown (the record's copy): pages then decide
// from what the browser reports (Kit.arch()). The packaging step writes each package's copy from that build's own
// v8_build_config.json, e.g. { v8_current_cpu: 'arm', has_maglev: true, source: '...' } — an expectation that does not come
// from the browser under test.
window.KIT_BUILD = null;
