# venetium's windows identity

these are the names, ids and guids that make venetium its own browser on windows - separate from chromium,
supermium and chrome. windows keys a lot off them: the user-data folder, the registry, taskbar pinning and toast
notifications, file and `http`/`https` associations, the default-browser registration, and group policy.

**do not change any of these once there is a release out.** if you do, existing users lose their profile, their
pins, their shortcuts, their default-browser choice and their notification identity, because windows will treat the
new build as a completely different app. i generated the guids once and that is final - that is why this file
exists, so nobody regenerates them by accident.

they are set in `chrome/install_static/chromium_install_modes.h` (the install-mode table) and in the policy files
listed below.

## names

| identity | value | where it is set | what it controls |
|---|---|---|---|
| product path | `Venetium` | `kProductPathName` | user data `%LOCALAPPDATA%\Venetium\User Data`, install dir, registry `Software\Venetium` |
| company path | *(empty)* | `kCompanyPathName` | no company folder level |
| AppUserModelID base | `Venetium` | `.base_app_id` | taskbar grouping / pins, toast identity |
| app name base | `Venetium` | `.base_app_name` | the registered application name |
| browser ProgID prefix | `VenetiumHTM` | `.browser_prog_id_prefix` | `.htm` / `.html` / `http(s)` association |
| browser ProgID description | `Venetium HTML Document` | `.browser_prog_id_description` | explorer "type" column |
| PDF ProgID prefix | `VenetiumPDF` | `.pdf_prog_id_prefix` | `.pdf` association |
| PDF ProgID description | `Venetium PDF Document` | `.pdf_prog_id_description` | |
| direct-launch URL scheme | `venetium` | `.direct_launch_url_scheme` | the `venetium://` launch scheme |
| safe-browsing client name | `venetium` | `kSafeBrowsingName` | the client id sent to the safe browsing api |
| registry path | `Software\Venetium` | derived from `kProductPathName` | per-user / per-machine settings |
| policy key | `SOFTWARE\Policies\Venetium` (+ `\Recommended`) | policy source generator + writer config + the setup crash client | keeps group policy isolated from every other chromium |
| browser executable | `venetium.exe` | patch 0002 | what task manager, explorer, shortcuts and "open with" show |

## guids

i replaced every guid that is not baked into generated COM type-library (MIDL) output, and kept the two that are -
changing those means regenerating MIDL, which is the painful part, and a per-user install never registers those
services anyway.

| role (mode-table field) | chromium / supermium value | venetium value | why |
|---|---|---|---|
| Active Setup GUID (`.active_setup_guid`) | `{7D2B3E1D-D096-4594-9D8F-A6667F12E0AC}` | `{D53748E5-629C-451F-B351-C090FB04B1A0}` | not MIDL-bound -> replaced |
| Toast activator CLSID (`.toast_activator_clsid`) | `{635EFA6F-08D6-4EC9-BD14-8A0FDE975159}` | `{2380AC27-D0C8-4B27-AE12-E3C8CECD4040}` | not MIDL-bound -> replaced |
| Elevation service CLSID (`.elevator_clsid`) | `{D133B120-6DB4-4D6B-8BFE-83BF8CA1B1B0}` | `{8A0E760E-2079-42F1-A4C4-2C6966E79C16}` | not MIDL-bound -> replaced |
| Elevation service IID + TypeLib (`.elevator_iid`) | `{BB19A0E5-00C6-4966-94B2-5AFEC6FED93A}` | **kept** | MIDL-bound (elevation_service_idl.idl + its generated output) |
| Tracing service CLSID (`.tracing_service_clsid`) | `{83F69367-442D-447F-8BCC-0E3F97BE9CF2}` | `{E1086662-B648-4ED4-ACB9-90B0D9C5A907}` | not MIDL-bound -> replaced |
| Tracing service IID + TypeLib (`.tracing_service_iid`) | `{A3FD580A-FFD4-4075-9174-75D0B199D3CB}` | **kept** | MIDL-bound (tracing_service_idl.idl + its generated output) |

the C++ initialiser forms, as written into the mode table:

- toast activator: `{0x2380AC27, 0xD0C8, 0x4B27, {0xAE, 0x12, 0xE3, 0xC8, 0xCE, 0xCD, 0x40, 0x40}}`
- elevator: `{0x8A0E760E, 0x2079, 0x42F1, {0xA4, 0xC4, 0x2C, 0x69, 0x66, 0xE7, 0x9C, 0x16}}`
- tracing service: `{0xE1086662, 0xB648, 0x4ED4, {0xAC, 0xB9, 0x90, 0xB0, 0xD9, 0xC5, 0xA9, 0x07}}`

the two kept IIDs are only used by the elevation and tracing **services**, which only a **system-level** install
registers. venetium on the surface is a per-user install, so they never come into play - which is why keeping them
is safe.

## left alone, on purpose

- `.sandbox_sid_prefix` is an appcontainer SID prefix, not a guid - left as chromium's. change it only if you want
  appcontainer isolation from other chromium installs too.
- `.app_guid` is empty (no google update integration), same as chromium.
