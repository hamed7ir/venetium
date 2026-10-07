NEVER regenerate. Changing any value after a release breaks users' profiles, pins, shortcuts, default-browser registration and notification identity.

# VENETIUM-IDS — the permanent Windows identity of Venetium

Created 2026-09-27 by BATCH-VENETIUM-1 §3.3 (the file did not exist before). Every later batch **reads and reuses**
these values. GUIDs were generated once with `python -c "import uuid; print(uuid.uuid4())"` (upper-cased).
Applied in `F:\cr\src\chrome\install_static\chromium_install_modes.h` (mode table) and the policy-key files listed below.

## Names

| identity | value | where it is set | what it controls |
|---|---|---|---|
| product path | `Venetium` | `kProductPathName` | user data `%LOCALAPPDATA%\Venetium\User Data`, install dir, registry `Software\Venetium` |
| company path | *(empty, unchanged)* | `kCompanyPathName` | no company folder level |
| AppUserModelID base | `Venetium` | `.base_app_id` | taskbar grouping / pins, toast identity |
| app name base | `Venetium` | `.base_app_name` | registered application name |
| browser ProgID prefix | `VenetiumHTM` | `.browser_prog_id_prefix` | .htm/.html/http(s) association |
| browser ProgID description | `Venetium HTML Document` | `.browser_prog_id_description` | Explorer "Type" column |
| PDF ProgID prefix | `VenetiumPDF` | `.pdf_prog_id_prefix` | .pdf association |
| PDF ProgID description | `Venetium PDF Document` | `.pdf_prog_id_description` | |
| direct-launch URL scheme | `venetium` | `.direct_launch_url_scheme` | `venetium://` launch scheme |
| safe-browsing client name | `venetium` | `kSafeBrowsingName` | client id sent to the Safe Browsing API |
| registry path | `Software\Venetium` | derived from `kProductPathName` (install_util) | per-user / per-machine settings |
| policy key | `SOFTWARE\Policies\Venetium` (+ `\Recommended`) | `components/policy/tools/generate_policy_source.py` `CHROMIUM_POLICY_KEY`; `template_writers/writer_configuration.py`; `chrome/installer/setup/installer_crash_reporter_client.cc` | group-policy isolation from every other Chromium |
| browser executable | `venetium.exe` | patch 0002 (BATCH-VENETIUM-1 §5) | what Task Manager, Explorer, shortcuts and "Open with" show |

## GUIDs

| role (mode-table field) | Chromium / Supermium value (old) | Venetium value | why |
|---|---|---|---|
| Active Setup GUID (`.active_setup_guid`) | `{7D2B3E1D-D096-4594-9D8F-A6667F12E0AC}` | `{D53748E5-629C-451F-B351-C090FB04B1A0}` | not IDL-bound → replaced |
| Toast activator CLSID (`.toast_activator_clsid`) | `{635EFA6F-08D6-4EC9-BD14-8A0FDE975159}` | `{2380AC27-D0C8-4B27-AE12-E3C8CECD4040}` | not IDL-bound → replaced |
| Elevation service CLSID (`.elevator_clsid`) | `{D133B120-6DB4-4D6B-8BFE-83BF8CA1B1B0}` | `{8A0E760E-2079-42F1-A4C4-2C6966E79C16}` | not IDL-bound → replaced |
| Elevation service IID + TypeLib (`.elevator_iid`) | `{BB19A0E5-00C6-4966-94B2-5AFEC6FED93A}` | **KEPT** | IDL-bound: `chrome/elevation_service/elevation_service_idl.idl:198` + `third_party/win_build_output/midl/chrome/elevation_service/*/` |
| Tracing service CLSID (`.tracing_service_clsid`) | `{83F69367-442D-447F-8BCC-0E3F97BE9CF2}` | `{E1086662-B648-4ED4-ACB9-90B0D9C5A907}` | not IDL-bound → replaced |
| Tracing service IID + TypeLib (`.tracing_service_iid`) | `{A3FD580A-FFD4-4075-9174-75D0B199D3CB}` | **KEPT** | IDL-bound: `chrome/windows_services/elevated_tracing_service/tracing_service_idl.idl:56` + MIDL outputs |

C++ initialiser forms (as written into the mode table):

- toast activator: `{0x2380AC27, 0xD0C8, 0x4B27, {0xAE, 0x12, 0xE3, 0xC8, 0xCE, 0xCD, 0x40, 0x40}}`
- elevator: `{0x8A0E760E, 0x2079, 0x42F1, {0xA4, 0xC4, 0x2C, 0x69, 0x66, 0xE7, 0x9C, 0x16}}`
- tracing service: `{0xE1086662, 0xB648, 0x4ED4, {0xAC, 0xB9, 0x90, 0xB0, 0xD9, 0xC5, 0xA9, 0x07}}`

The two kept IIDs are only used by the elevation and tracing **services**, which a **system-level** install registers;
Venetium on the Surface is a per-user install. Changing them would mean regenerating MIDL output — the wall the CR109
porter hit.

## Not changed, noted

- `.sandbox_sid_prefix` `S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-924012148-` — an AppContainer
  SID prefix, not a GUID; left as Chromium's (the batch changes GUIDs only). His call if AppContainer isolation from
  other Chromium installs is wanted.
- `.app_guid` — empty (no Google Update integration), as in Chromium.
