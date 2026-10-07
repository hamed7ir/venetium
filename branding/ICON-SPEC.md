# ICON-SPEC — every Windows image that shows the product logo (BATCH-VENETIUM-1 §1.2)

Measured 2026-09-26 on `F:\cr\src` (Chromium 150.0.7871.226 + Supermium's seven patches, which change **no image**).
Sizes by Pillow 12.3.0; ICO entries by parsing the ICONDIR (`s4\inv\assets_inventory.py`, raw data in
`s4\inv\assets-inventory.json`, originals rendered in `s4\inv\assets-originals.png`).
Branding path on Windows is `chromium` (`branding_path_component`; `is_chrome_branded=false`).

**Start tile background colour (current): `#212121`** — `chrome/app/visual_elements_resources/chrome.VisualElementsManifest.xml`,
`BackgroundColor="#212121"`, `ForegroundText="light"`, `ShowNameOnSquare150x150Logo="on"`. Unchanged by this batch; his to change.

Legend — **Action**: `WRITE` = rendered from `Venetium.png` / his `Venetium.ico` over the same file name ·
`CANNOT` = cannot be made from a square icon → **left as Chromium's original, listed for him** (§2.3).

| # | asset (under `F:\cr\src\`) | format | pixel size(s) | where it shows (reference) | action |
|---|---|---|---|---|---|
| 1 | `chrome\app\theme\chromium\win\chromium.ico` | ICO | 16/8bpp, 32/8, 48/8, 16/32bpp, 32/32, 48/32, 256/32 (PNG) — 7 entries | `IDR_MAINFRAME` (`chrome_exe.rc:59`, `chrome_dll.rc:177`): exe, window, taskbar, shortcuts | WRITE — his ICO + 24×24 (§2.1) |
| 2 | `chrome\app\theme\chromium\win\chromium_doc.ico` | ICO | same 7 entries as #1 | `IDR_X006_HTML_DOC` (`chrome_exe.rc:78`): .htm/.html file icon | **CANNOT** — a document page with the logo as a badge |
| 3 | `chrome\app\theme\chromium\win\chromium_pdf.ico` | ICO | same 7 entries as #1 | `IDR_X007_PDF_DOC` (`chrome_exe.rc:79`): .pdf file icon | **CANNOT** — page + logo badge + "PDF" label |
| 4 | `chrome\app\theme\chromium\win\tiles\Logo.png` | PNG RGBA | 600×600 (logo = centred 220×220, **36.7 %** of the tile, transparent padding) | Start tile `Square150x150Logo` (`chrome/BUILD.gn:1502` `visual_elements_resources`) | WRITE — logo at the same 36.7 % centred box; tile colour stays `#212121` |
| 5 | `chrome\app\theme\chromium\win\tiles\SmallLogo.png` | PNG RGBA | 176×176 (logo = centred 118×118, **67.0 %**) | Start tile `Square70x70Logo` + `Square44x44Logo` (same) | WRITE — same 67.0 % box |
| 6 | `chrome\app\theme\chromium\product_logo_16.png` | PNG (P) | 16×16 | `IDR_PRODUCT_LOGO_16_SHORTCUTS` (`chrome_unscaled_resources.grd:91`) | WRITE |
| 7 | `chrome\app\theme\chromium\product_logo_24.png` | PNG RGBA | 24×24 | `IDR_PRODUCT_LOGO_24_SHORTCUTS` (`:92`) | WRITE |
| 8 | `chrome\app\theme\chromium\product_logo_64.png` | PNG RGBA | 64×64 | `IDR_PRODUCT_LOGO_64` (`:24`), `IDR_PRODUCT_LOGO_64_SHORTCUTS` (`:93`) | WRITE |
| 9 | `chrome\app\theme\chromium\product_logo_128.png` | PNG RGBA | 128×128 | `IDR_PRODUCT_LOGO_128` (`:25`), `_SHORTCUTS` (`:94`) | WRITE |
| 10 | `chrome\app\theme\chromium\product_logo_256.png` | PNG RGBA | 256×256 | `IDR_PRODUCT_LOGO_256` (`:26`) | WRITE |
| 11 | `chrome\app\theme\chromium\product_logo.svg` | SVG (vector, viewBox 256×256, no raster, no animation) | vector | `IDR_PRODUCT_LOGO_SVG` (`:87`): WebUI logo (About, first-run) | WRITE — **raster-in-SVG**: an SVG of the same viewBox embedding `Venetium.png` at 512×512 (no vector master exists; a hand-made `Venetium.svg` would replace it) |
| 12 | `chrome\app\theme\chromium\product_logo_animation.svg` | SVG (vector, viewBox 160×160, **animated**) | vector | `IDR_PRODUCT_LOGO_ANIMATION_SVG` (`:88`) | **CANNOT** — an animation of Chromium's logo geometry |
| 13 | `chrome\app\theme\default_100_percent\chromium\product_logo_16.png` | PNG RGBA | 16×16 | `IDR_PRODUCT_LOGO_16` @1x (`theme_resources.grd:198`) | WRITE |
| 14 | `chrome\app\theme\default_200_percent\chromium\product_logo_16.png` | PNG RGBA | 32×32 | `IDR_PRODUCT_LOGO_16` @2x | WRITE |
| 15 | `chrome\app\theme\default_100_percent\chromium\product_logo_32.png` | PNG RGBA | 32×32 | `IDR_PRODUCT_LOGO_32` @1x (`theme_resources.grd:166`) | WRITE |
| 16 | `chrome\app\theme\default_200_percent\chromium\product_logo_32.png` | PNG (P) | 64×64 | `IDR_PRODUCT_LOGO_32` @2x | WRITE |
| 17 | `chrome\app\theme\default_100_percent\chromium\product_logo_name_22.png` | PNG RGBA | 97×22 (non-square) | `IDR_PRODUCT_LOGO_NAME_22` @1x (`theme_resources.grd:207`) | **CANNOT** — logo + the word "chromium" (wordmark) |
| 18 | `chrome\app\theme\default_200_percent\chromium\product_logo_name_22.png` | PNG RGBA | 194×44 | same @2x | **CANNOT** — wordmark |
| 19 | `chrome\app\theme\default_100_percent\chromium\product_logo_name_22_white.png` | PNG RGBA | 97×22 | `IDR_PRODUCT_LOGO_NAME_22_WHITE` @1x (`:208`) | **CANNOT** — wordmark (white text) |
| 20 | `chrome\app\theme\default_200_percent\chromium\product_logo_name_22_white.png` | PNG RGBA | 194×44 | same @2x | **CANNOT** — wordmark |
| 21 | `components\resources\default_100_percent\chromium\product_logo.png` | PNG (P) | 171×32 | `IDR_PRODUCT_LOGO` @1x (`version_ui_scaled_resources.grdp:17`): chrome://version header | **CANNOT** — wordmark |
| 22 | `components\resources\default_200_percent\chromium\product_logo.png` | PNG (P) | 342×64 | same @2x | **CANNOT** — wordmark |
| 23 | `components\resources\default_100_percent\chromium\product_logo_white.png` | PNG (P) | 171×32 | `IDR_PRODUCT_LOGO_WHITE` @1x (`:18`) | **CANNOT** — wordmark |
| 24 | `components\resources\default_200_percent\chromium\product_logo_white.png` | PNG (P) | 342×64 | same @2x | **CANNOT** — wordmark |
| 25 | `components\resources\default_100_percent\chromium\favicon_product.png` | PNG (P) | 16×16 | `IDR_PRODUCT_FAVICON` @1x (`:19`): chrome://version tab icon | WRITE |
| 26 | `components\resources\default_200_percent\chromium\favicon_product.png` | PNG RGBA | 32×32 | same @2x | WRITE |
| 27 | `chrome\browser\resources\intro\images\refresh_showcase_illustration_chromium.png` | PNG RGBA | 800×800 | first-run intro "showcase" (`chrome/browser/resources/intro/BUILD.gn:30`) | **CANNOT** — an illustration with the logo composited into artwork |

**Totals:** 27 assets · **WRITE 15** (1 ICO, 13 PNG, 1 SVG) · **CANNOT 12** (2 document ICOs, 1 animated SVG,
8 wordmarks, 1 illustration). Every CANNOT row keeps Chromium's image until he supplies a replacement with the same file
name and pixel size.

## Reviewed and excluded (not the product logo, or not Windows)

| asset | why excluded |
|---|---|
| `chrome\app\theme\chromium\win\app_list.ico` (16/32/48 ×8+32bpp, 256) | `IDR_X001_APP_LIST` — an app-grid glyph, no product logo |
| `chrome\app\theme\chromium\win\incognito.ico` (17×16, 33×33, 49×48, 256) | `IDR_X003_INCOGNITO` — the incognito glyph, no product logo |
| `chrome\app\theme\default_{100,200}_percent\chromium\favicon_password_manager.png` | `IDR_PASSWORD_MANAGER_FAVICON` — a key glyph |
| `chrome\app\theme\default_{100,200}_percent\chromium\webstore_icon*.png` | Chrome Web Store icon, not the product |
| `chrome\app\theme\chromium\product_logo_22_mono.png` | `IDR_STATUS_TRAY_ICON`, `is_macosx` only |
| `chrome\app\theme\chromium\product_logo_48.png` | `linux_cros_extra_data` only (`chrome/BUILD.gn:1829`) |
| `chrome\app\theme\chromium\{linux,mac,chromeos}\*` | other platforms |
| `chrome\app\theme\chromium\chromium.ai`, `product_logo.ai` | design sources, never built |

## Inputs
- `Venetium.png` — 1254×1254 RGBA; alpha bbox (0,0)–(1253,1254): a full-bleed rounded square (76 % opaque pixels).
- `Venetium.ico` — 6 entries 16/32/48/64/128/256, all 32-bpp **DIB** (none PNG-compressed).
- The exe ICO (#1) gains **24×24** (§2.1). Every size `chromium.ico` carries (16/32/48/256) is already in his ICO;
  its three **8-bpp** entries (for 256-colour displays) are a bit-depth, not a size, and are not recreated.
