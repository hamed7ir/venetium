Venetium icons
==============

Run set-icon.exe (in the Venetium folder, next to venetium.exe) to choose the
browser icon:
  Venetium
  Venetium, WinRT style (flat - also gives a blue Start tile)
  Venetium W10M (the logo of Venetium for Windows 10 Mobile - dark Start tile)
  My own .ico (any .ico file - you can also drop it onto set-icon.exe)

It writes venetium-icon.ico next to venetium.exe (Venetium uses it for its
windows and taskbar button), sets the Start tile, and points every Venetium
shortcut and pin at the icon. Close Venetium before you switch and start it
again afterwards. If a pin or the Start tile still shows the old icon, unpin
Venetium and pin it again (Windows keeps old icons in a cache).

Your own icon also goes on the Start tile and in the app list: set-icon.exe
draws tile pictures from it (icons\own-logo.png, icons\own-smalllogo.png) on a
tile colour taken from the icon. An icon with no frame of at least 96 px would
look blurry there, so then Windows draws the tile from the icon itself.

Command line: set-icon.exe classic | winrt | w10m | "C:\path\my.ico"  [--quiet]

Files here: venetium-winrt.ico (the WinRT-style icon) with winrt-logo.png and
winrt-smalllogo.png (its Start tile pictures); venetium-w10m.ico with
w10m-logo.png and w10m-smalllogo.png; tile-classic.xml, tile-winrt.xml and
tile-w10m.xml (the Start tile settings, copied over
venetium.VisualElementsManifest.xml).

venetium-w10m.ico is the logo of Venetium for Windows 10 Mobile by Computershik
(https://github.com/Computershik73/venetium-w10m, GPL-3.0), drawn from its
tools/make-assets.py at every icon size, on its tile colour #1C1B22.
