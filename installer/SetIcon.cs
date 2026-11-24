// Venetium 0074: set-icon.exe - chooses the browser icon for the Venetium folder it sits in (next to venetium.exe; the
// portable zip and an installed Venetium both have it). The same choices as Setup's icon row, the same code
// (IconTools.cs): no PowerShell, so it also works where PowerShell runs in Constrained Language mode (most Windows RT
// devices).
//   set-icon.exe                    a small window with the choices
//   set-icon.exe classic|winrt|w10m the built-in icons (venetium = classic)
//   set-icon.exe "C:\my.ico"        your own icon (dropping an .ico onto set-icon.exe does the same)
//   add --quiet for no message box. Exit codes: 0 done, 1 failed, 2 not a choice / not a valid .ico,
//   3 venetium.exe is not next to set-icon.exe.
using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

[assembly: AssemblyTitle("Venetium icon")]
[assembly: AssemblyProduct("Venetium")]

namespace VenetiumSetup
{
    internal static class SetIconProgram
    {
        internal const string Title = "Venetium icon";

        [STAThread]
        private static int Main(string[] args)
        {
            string dir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
            string exe = Path.Combine(dir, "venetium.exe");
            bool quiet = false; string arg = null;
            foreach (var a in args)
            {
                if (string.Equals(a, "--quiet", StringComparison.OrdinalIgnoreCase) || string.Equals(a, "/quiet", StringComparison.OrdinalIgnoreCase)) quiet = true;
                else if (arg == null) arg = a;
                else return 2;
            }
            if (!File.Exists(exe))
            {
                if (!quiet) MessageBox.Show("Put set-icon.exe in the Venetium folder (next to venetium.exe) and run it there.", Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return 3;
            }
            if (arg == null)
            {
                Application.EnableVisualStyles();
                Application.Run(new SetIconForm(dir, exe));
                return 0;
            }
            IconChoice choice; string own = null;
            if (!IconFiles.TryParse(arg, out choice))
            {
                string path = arg.Trim().Trim('"');
                if (!IconFiles.IsIco(path))
                {
                    if (!quiet) MessageBox.Show("\"" + path + "\" is not a valid .ico icon (or not one of: classic, winrt, w10m).", Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return 2;
                }
                choice = IconChoice.Own; own = Path.GetFullPath(path);
            }
            try
            {
                int n = Run(dir, exe, choice, own);
                if (!quiet) MessageBox.Show(DoneText(n), Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return 0;
            }
            catch (Exception ex)
            {
                if (!quiet) MessageBox.Show("Could not set the icon:\n\n" + ex.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        internal static int Run(string dir, string exe, IconChoice choice, string own)
        {
            string icon = IconFiles.Apply(dir, choice, own);
            return IconFiles.UpdateShortcuts(exe, icon);
        }

        internal static string DoneText(int n)
        {
            return "Done - " + n + " shortcut(s) and pin(s) updated.\n\nStart Venetium again. If a taskbar pin or the Start tile still " +
                   "shows the old icon, unpin Venetium and pin it again (Windows keeps old icons in a cache).";
        }
    }

    internal sealed class SetIconForm : Form
    {
        private readonly string _dir, _exe;
        private readonly RadioButton _venetium, _winrt, _w10m, _own;
        private readonly Button _pick, _apply, _close;
        private readonly Label _file;
        private string _ownIcon;

        internal SetIconForm(string dir, string exe)
        {
            _dir = dir; _exe = exe;
            Text = SetIconProgram.Title;
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(440, 262);
            BackColor = Color.White;
            Font = new Font("Segoe UI", 9f);

            var head = new Label { Left = 18, Top = 14, Width = 404, Height = 20, Text = "The icon for Venetium's windows, taskbar, shortcuts and Start tile:" };
            _venetium = new RadioButton { Left = 22, Top = 42, Width = 380, Height = 26, Text = "Venetium" };
            _winrt = new RadioButton { Left = 22, Top = 70, Width = 380, Height = 26, Text = "Venetium, WinRT style" };
            _w10m = new RadioButton { Left = 22, Top = 98, Width = 380, Height = 26, Text = "Venetium W10M" };
            _own = new RadioButton { Left = 22, Top = 126, Width = 140, Height = 26, Text = "My own .ico" };
            _pick = new Button { Left = 166, Top = 125, Width = 128, Height = 28, Text = "Choose .ico..." };
            _file = new Label { Left = 40, Top = 156, Width = 382, Height = 36, ForeColor = Color.Gray, AutoEllipsis = true, Text = "" };
            _apply = new Button { Left = 246, Top = 218, Width = 84, Height = 30, Text = "Apply" };
            _close = new Button { Left = 338, Top = 218, Width = 84, Height = 30, Text = "Close" };

            switch (IconFiles.Current(dir))   // start on what the folder has now
            {
                case IconChoice.WinRT: _winrt.Checked = true; break;
                case IconChoice.W10M: _w10m.Checked = true; break;
                case IconChoice.Own: _own.Checked = true; _ownIcon = Path.Combine(dir, IconFiles.IconOverride); break;
                default: _venetium.Checked = true; break;
            }
            ShowOwn();
            _own.CheckedChanged += (s, e) => { ShowOwn(); if (_own.Checked && _ownIcon == null) Pick(); };
            _pick.Click += (s, e) => { _own.Checked = true; Pick(); };
            _apply.Click += (s, e) => ApplyChoice();
            _close.Click += (s, e) => Close();
            Controls.AddRange(new Control[] { head, _venetium, _winrt, _w10m, _own, _pick, _file, _apply, _close });
            AcceptButton = _apply; CancelButton = _close;
        }

        private void ShowOwn()
        {
            _file.Text = !_own.Checked ? "" : _ownIcon == null ? "No icon file chosen yet." : "Your icon: " + _ownIcon;
        }

        private void Pick()
        {
            using (var d = new OpenFileDialog { Filter = "Icons (*.ico)|*.ico", Title = "Choose an icon for Venetium" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                if (!IconFiles.IsIco(d.FileName)) { MessageBox.Show(this, "That file is not a valid .ico icon.", SetIconProgram.Title, MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
                _ownIcon = d.FileName;
                ShowOwn();
            }
        }

        private void ApplyChoice()
        {
            IconChoice c = _winrt.Checked ? IconChoice.WinRT : _w10m.Checked ? IconChoice.W10M : _own.Checked ? IconChoice.Own : IconChoice.Venetium;
            if (c == IconChoice.Own && _ownIcon == null) { MessageBox.Show(this, "Choose your .ico file first (Choose .ico...).", SetIconProgram.Title); return; }
            UseWaitCursor = true; Enabled = false;
            try
            {
                int n = SetIconProgram.Run(_dir, _exe, c, _ownIcon);
                if (c == IconChoice.Own) { _ownIcon = Path.Combine(_dir, IconFiles.IconOverride); ShowOwn(); }
                MessageBox.Show(this, SetIconProgram.DoneText(n), SetIconProgram.Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex) { MessageBox.Show(this, "Could not set the icon:\n\n" + ex.Message, SetIconProgram.Title, MessageBoxButtons.OK, MessageBoxIcon.Error); }
            finally { Enabled = true; UseWaitCursor = false; }
        }
    }
}
