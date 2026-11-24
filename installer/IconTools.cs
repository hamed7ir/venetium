// Venetium 0074: the browser icon choice, shared by Setup (Setup.cs) and set-icon.exe (SetIcon.cs).
//
// venetium-icon.ico beside venetium.exe is what the browser shows for its windows and taskbar button
// (chrome/browser/win/app_icon.cc); the shortcuts point at it; venetium.VisualElementsManifest.xml (the Start tile and
// the Windows 10 app list) follows the choice. Everything here is plain .NET 4.0 + shell COM - no PowerShell, so it
// also works where PowerShell is in Constrained Language mode (most Windows RT devices).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace VenetiumSetup
{
    /// <summary>The browser icon the user picked.</summary>
    internal enum IconChoice { Venetium, WinRT, W10M, Own }

    internal static class IconFiles
    {
        internal const string IconOverride = "venetium-icon.ico";
        internal const string Manifest = "venetium.VisualElementsManifest.xml";

        /// <summary>True when the file starts like a Windows icon (ICONDIR: reserved 0, type 1, at least one image).</summary>
        internal static bool IsIco(string path)
        {
            try
            {
                using (var f = File.OpenRead(path))
                {
                    var b = new byte[6];
                    return f.Read(b, 0, 6) == 6 && b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0 && (b[4] | b[5]) != 0;
                }
            }
            catch { return false; }
        }

        /// <summary>"classic"/"venetium", "winrt", "w10m" -> that choice; anything else is not a built-in name.</summary>
        internal static bool TryParse(string name, out IconChoice choice)
        {
            string n = (name ?? "").Trim().ToLowerInvariant();
            choice = n == "winrt" ? IconChoice.WinRT : n == "w10m" ? IconChoice.W10M : IconChoice.Venetium;
            return n == "classic" || n == "venetium" || n == "winrt" || n == "w10m";
        }

        /// <summary>Applies a choice to the Venetium folder: writes (or removes) venetium-icon.ico, sets the Start tile
        /// manifest (for an own icon: tile pictures made from it, or no manifest when it is too small for a tile) and
        /// returns what the shortcuts should show (venetium-icon.ico, or venetium.exe for the normal icon).</summary>
        internal static string Apply(string dir, IconChoice icon, string ownIcon)
        {
            string iconFile = Path.Combine(dir, IconOverride);
            string iconsDir = Path.Combine(dir, "icons");
            if (icon == IconChoice.Venetium)
            {
                if (File.Exists(iconFile)) File.Delete(iconFile);   // an earlier choice of another icon
            }
            else
            {
                string from = icon == IconChoice.WinRT ? Path.Combine(iconsDir, "venetium-winrt.ico")
                            : icon == IconChoice.W10M ? Path.Combine(iconsDir, "venetium-w10m.ico") : ownIcon;
                if (string.IsNullOrEmpty(from) || !IsIco(from)) throw new InvalidDataException("The icon file is not a valid .ico: " + from);
                if (!SamePath(from, iconFile)) File.Copy(from, iconFile, true);   // re-applying the current own icon
            }
            string manifest = Path.Combine(dir, Manifest);
            OwnTile.Clean(iconsDir);   // tile pictures made for an earlier "own icon" choice
            if (icon == IconChoice.Own)
            {
                // the user's icon on the Start tile and in the app list too; an icon too small for a tile (or one that
                // cannot be read) gets no manifest at all, so Windows draws the tile from the shortcut's icon itself
                if (!OwnTile.Write(iconFile, iconsDir, manifest) && File.Exists(manifest)) File.Delete(manifest);
            }
            else
            {
                string tile = Path.Combine(iconsDir, icon == IconChoice.WinRT ? "tile-winrt.xml"
                                                   : icon == IconChoice.W10M ? "tile-w10m.xml" : "tile-classic.xml");
                if (File.Exists(tile)) File.Copy(tile, manifest, true);
            }
            return File.Exists(iconFile) ? iconFile : Path.Combine(dir, "venetium.exe");
        }

        /// <summary>Which choice the folder has now (for set-icon.exe's window): no venetium-icon.ico = Venetium; the same
        /// bytes as a built-in icon = that one; anything else = the user's own.</summary>
        internal static IconChoice Current(string dir)
        {
            string iconFile = Path.Combine(dir, IconOverride);
            if (!File.Exists(iconFile)) return IconChoice.Venetium;
            if (SameBytes(iconFile, Path.Combine(dir, "icons", "venetium-winrt.ico"))) return IconChoice.WinRT;
            if (SameBytes(iconFile, Path.Combine(dir, "icons", "venetium-w10m.ico"))) return IconChoice.W10M;
            return IconChoice.Own;
        }

        /// <summary>Points every shortcut and pin that starts this venetium.exe at the icon (desktop, Start menu, taskbar
        /// and Start pins - user and all-users). Saving a Start shortcut also makes Windows read the tile manifest again.
        /// Returns how many were updated; all-users ones need admin rights and are skipped without them.</summary>
        internal static int UpdateShortcuts(string exe, string icon)
        {
            var dirs = new List<string>();
            foreach (var f in new[] { Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolder.CommonDesktopDirectory,
                                      Environment.SpecialFolder.StartMenu, Environment.SpecialFolder.CommonStartMenu })
                try { string d = Environment.GetFolderPath(f); if (!string.IsNullOrEmpty(d)) dirs.Add(d); } catch { }
            dirs.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), @"Microsoft\Internet Explorer\Quick Launch"));
            string full = Path.GetFullPath(exe);
            int n = 0;
            foreach (var d in dirs)
            {
                if (!Directory.Exists(d)) continue;
                foreach (var lnk in Lnks(d))
                {
                    string target = Shortcut.TargetOf(lnk);
                    try { if (target == null || !string.Equals(Path.GetFullPath(target), full, StringComparison.OrdinalIgnoreCase)) continue; }
                    catch { continue; }
                    if (Shortcut.SetIcon(lnk, icon)) n++;
                }
            }
            return n;
        }

        private static IEnumerable<string> Lnks(string root)
        {
            var stack = new Stack<string>(); stack.Push(root);
            while (stack.Count > 0)
            {
                string d = stack.Pop(); string[] files = null, subs = null;
                try { files = Directory.GetFiles(d, "*.lnk"); subs = Directory.GetDirectories(d); } catch { }
                if (files != null) foreach (var f in files) yield return f;
                if (subs != null) foreach (var s in subs) stack.Push(s);
            }
        }

        private static bool SamePath(string a, string b)
        {
            try { return string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase); } catch { return false; }
        }

        private static bool SameBytes(string a, string b)
        {
            try
            {
                if (!File.Exists(a) || !File.Exists(b) || new FileInfo(a).Length != new FileInfo(b).Length) return false;
                byte[] x = File.ReadAllBytes(a), y = File.ReadAllBytes(b);
                for (int i = 0; i < x.Length; i++) if (x[i] != y[i]) return false;
                return true;
            }
            catch { return false; }
        }
    }

    /// <summary>Venetium 0074: Start tile pictures made from the user's own .ico, so the Start tile and the Windows 10 app
    /// list show that icon as well. The .ico's largest frame (a DIB or a PNG frame) is drawn into icons\own-logo.png
    /// (600 x 600) and icons\own-smalllogo.png (176 x 176), on a tile colour taken from the icon (its average colour at
    /// 30 % lightness, so the white tile name stays readable), and a manifest pointing at them is written. A largest
    /// frame under 96 px would look blurry on a tile: then nothing is written and the caller removes the manifest, so
    /// Windows draws the tile from the shortcut's icon itself.</summary>
    internal static class OwnTile
    {
        internal const string Logo = "own-logo.png", SmallLogo = "own-smalllogo.png";
        internal const int MinFrame = 96;

        internal static void Clean(string iconsDir)
        {
            foreach (var n in new[] { Logo, SmallLogo })
                try { string p = Path.Combine(iconsDir, n); if (File.Exists(p)) File.Delete(p); } catch { }
        }

        internal static bool Write(string ico, string iconsDir, string manifest)
        {
            try
            {
                using (Bitmap src = LargestFrame(ico))
                {
                    if (src == null) return false;
                    int size = Math.Max(src.Width, src.Height);
                    Directory.CreateDirectory(iconsDir);
                    Draw(src, Path.Combine(iconsDir, Logo), 600, Math.Min(300, 2 * size), 276);
                    Draw(src, Path.Combine(iconsDir, SmallLogo), 176, Math.Min(116, size * 5 / 4), 88);
                    File.WriteAllText(manifest,
                        "<!-- Venetium Start tile for your own icon (the pictures were made from it). -->\r\n" +
                        "<Application xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\r\n" +
                        "  <VisualElements\r\n" +
                        "    BackgroundColor=\"" + TileColor(src) + "\"\r\n" +
                        "    ShowNameOnSquare150x150Logo=\"on\"\r\n" +
                        "    ForegroundText=\"light\"\r\n" +
                        "    Square150x150Logo=\"icons\\" + Logo + "\"\r\n" +
                        "    Square70x70Logo=\"icons\\" + SmallLogo + "\"\r\n" +
                        "    Square44x44Logo=\"icons\\" + SmallLogo + "\"/>\r\n" +
                        "</Application>\r\n", Encoding.ASCII);
                    return true;
                }
            }
            catch { Clean(iconsDir); return false; }
        }

        /// <summary>The .ico's widest frame as a 32-bit ARGB bitmap, or null when it is under MinFrame.</summary>
        private static Bitmap LargestFrame(string ico)
        {
            byte[] d = File.ReadAllBytes(ico);
            int n = BitConverter.ToUInt16(d, 4), best = -1, bw = 0, bh = 0;
            for (int i = 0; i < n; i++)
            {
                int w = d[6 + 16 * i] == 0 ? 256 : d[6 + 16 * i], h = d[7 + 16 * i] == 0 ? 256 : d[7 + 16 * i];
                if (w > bw) { bw = w; bh = h; best = i; }
            }
            if (best < 0 || bw < MinFrame) return null;
            int len = BitConverter.ToInt32(d, 6 + 16 * best + 8), off = BitConverter.ToInt32(d, 6 + 16 * best + 12);
            if (d[off] == 0x89 && d[off + 1] == (byte)'P' && d[off + 2] == (byte)'N' && d[off + 3] == (byte)'G')
                using (var ms = new MemoryStream(d, off, len))
                using (var img = Image.FromStream(ms))
                    return new Bitmap(img);
            // a DIB frame: an .ico with only this entry, loaded at its own size (no scaling)
            var one = new byte[22 + len];
            Array.Copy(d, 0, one, 0, 4); one[4] = 1;
            Array.Copy(d, 6 + 16 * best, one, 6, 12);
            BitConverter.GetBytes(22).CopyTo(one, 18);
            Array.Copy(d, off, one, 22, len);
            using (var ms = new MemoryStream(one))
            using (var icon = new Icon(ms, bw, bh))
                return icon.ToBitmap();
        }

        private static void Draw(Bitmap src, string path, int canvas, int box, int cy)
        {
            using (var bmp = new Bitmap(canvas, canvas, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(bmp))
            using (var ia = new System.Drawing.Imaging.ImageAttributes())
            {
                g.Clear(Color.Transparent);
                g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                g.CompositingQuality = System.Drawing.Drawing2D.CompositingQuality.HighQuality;
                ia.SetWrapMode(System.Drawing.Drawing2D.WrapMode.TileFlipXY);   // no dark fringe at the edges
                float s = (float)box / Math.Max(src.Width, src.Height);
                int w = (int)Math.Round(src.Width * s), h = (int)Math.Round(src.Height * s);
                g.DrawImage(src, new Rectangle((canvas - w) / 2, cy - h / 2, w, h), 0, 0, src.Width, src.Height, GraphicsUnit.Pixel, ia);
                bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            }
        }

        /// <summary>The icon's alpha-weighted average colour, set to 30 % lightness (saturation at most 80 %).</summary>
        private static string TileColor(Bitmap src)
        {
            double r = 0, g = 0, b = 0, a = 0;
            using (var small = new Bitmap(16, 16, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
            {
                using (var gr = Graphics.FromImage(small))
                {
                    gr.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                    gr.DrawImage(src, new Rectangle(0, 0, 16, 16));
                }
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++)
                    {
                        Color c = small.GetPixel(x, y);
                        r += c.R * c.A; g += c.G * c.A; b += c.B * c.A; a += c.A;
                    }
            }
            if (a < 1) return "#182A4B";
            r /= a * 255; g /= a * 255; b /= a * 255;
            double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), l = (max + min) / 2, h = 0, s = 0;
            if (max > min)
            {
                double dd = max - min;
                s = l > 0.5 ? dd / (2 - max - min) : dd / (max + min);
                h = max == r ? (g - b) / dd + (g < b ? 6 : 0) : max == g ? (b - r) / dd + 2 : (r - g) / dd + 4;
                h /= 6;
            }
            s = Math.Min(s, 0.8); l = 0.3;
            double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
            Func<double, double> hue = t =>
            {
                if (t < 0) t += 1; if (t > 1) t -= 1;
                return t < 1.0 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6 : p;
            };
            return string.Format("#{0:X2}{1:X2}{2:X2}", (int)Math.Round(hue(h + 1.0 / 3) * 255), (int)Math.Round(hue(h) * 255), (int)Math.Round(hue(h - 1.0 / 3) * 255));
        }
    }

    /// <summary>Creates a .lnk via the shell's IShellLink COM object (works on RT desktop). Finestra's, plus SetIcon.</summary>
    internal static class Shortcut
    {
        internal static void Create(string lnkPath, string target, string args, string workDir, string iconPath, string desc)
        {
            try
            {
                var link = (IShellLinkW)new CShellLink();
                link.SetPath(target);
                if (!string.IsNullOrEmpty(args)) link.SetArguments(args);
                if (!string.IsNullOrEmpty(workDir)) link.SetWorkingDirectory(workDir);
                if (!string.IsNullOrEmpty(iconPath)) link.SetIconLocation(iconPath, 0);
                if (!string.IsNullOrEmpty(desc)) link.SetDescription(desc);
                ((IPersistFile)link).Save(lnkPath, true);
            }
            catch { /* shortcut is best-effort */ }
        }

        /// <summary>The target path a .lnk points at, or null when it cannot be read.</summary>
        internal static string TargetOf(string lnkPath)
        {
            try
            {
                var link = (IShellLinkW)new CShellLink();
                ((IPersistFile)link).Load(lnkPath, 0);
                var sb = new StringBuilder(1024);
                link.GetPath(sb, sb.Capacity, IntPtr.Zero, 0);
                return sb.Length > 0 ? sb.ToString() : null;
            }
            catch { return null; }
        }

        /// <summary>Venetium 0074: changes only the icon of an existing .lnk; false when it cannot be read or saved.</summary>
        internal static bool SetIcon(string lnkPath, string iconPath)
        {
            try
            {
                var link = (IShellLinkW)new CShellLink();
                ((IPersistFile)link).Load(lnkPath, 2 /* STGM_READWRITE */);
                link.SetIconLocation(iconPath, 0);
                ((IPersistFile)link).Save(lnkPath, true);
                return true;
            }
            catch { return false; }
        }

        [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
        private class CShellLink { }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("000214F9-0000-0000-C000-000000000046")]
        private interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszFile, int cch, IntPtr pfd, int fFlags);
            void GetIDList(out IntPtr ppidl);
            void SetIDList(IntPtr pidl);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszName, int cch);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string pszName);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszDir, int cch);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string pszDir);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszArgs, int cch);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string pszArgs);
            void GetHotkey(out short pwHotkey);
            void SetHotkey(short wHotkey);
            void GetShowCmd(out int piShowCmd);
            void SetShowCmd(int iShowCmd);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszIconPath, int cch, out int piIcon);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string pszIconPath, int iIcon);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string pszPathRel, int dwReserved);
            void Resolve(IntPtr hwnd, int fFlags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string pszFile);
        }

        [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("0000010b-0000-0000-C000-000000000046")]
        private interface IPersistFile
        {
            void GetClassID(out Guid pClassID);
            [PreserveSig] int IsDirty();
            void Load([MarshalAs(UnmanagedType.LPWStr)] string pszFileName, int dwMode);
            void Save([MarshalAs(UnmanagedType.LPWStr)] string pszFileName, [MarshalAs(UnmanagedType.Bool)] bool fRemember);
            void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string pszFileName);
            void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string ppszFileName);
        }
    }
}
