Venetium is built from the Chromium open-source project (BSD-3-Clause, The Chromium Authors), with patches from Supermium by win32ss, and compiled with llvm-rt2 and rustc-rt2.

---

## Supermium

Venetium includes patches from Supermium by win32ss — https://github.com/win32ss/supermium

Venetium credits Supermium in `chrome://credits` through `third_party/supermium_patches/README.chromium` and its
`NOTICE` file (patch `0001-branding.patch`).

## EasyList and EasyPrivacy

Venetium's built-in ad and tracker blocker (patch 0059) ships `venetium_filters\filters.dat`, made from EasyList and
EasyPrivacy by The EasyList authors — https://easylist.to/ — unmodified, converted by Chromium's `ruleset_converter`.
They are dual licensed under the GNU GPL v3 or later and CC BY-SA 3.0 or later; Venetium distributes them under the
GPL v3. Every package carries `venetium_filters\LICENSE.txt` (source: `package/venetium_filters/LICENSE.txt`).
