# 動作確認用の操作（PowerShell、dot-source して使う：. .\tools\drive.ps1）
# OS のキー送信・マウス送信（SendInput / SetCursorPos）は使わず、TLAP の窓へ PostMessage で直接メッセージを投げる。
# 注意：Shift / Ctrl を押した状態は PostMessage では届かない（GetKeyState は本物のキーボード状態を見る）ので、
#       Shift+矢印・Ctrl+O は確かめられない（メニューの項目は WM_COMMAND で確かめる）。
Add-Type -ReferencedAssemblies System.Drawing @'
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public static class Drv {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static void Shot(IntPtr h, string path) {
    RECT c; GetClientRect(h, out c); POINT p; p.X = 0; p.Y = 0; ClientToScreen(h, ref p);
    using (Bitmap b = new Bitmap(c.R, c.B)) {
      using (Graphics g = Graphics.FromImage(b)) g.CopyFromScreen(p.X, p.Y, 0, 0, new Size(c.R, c.B));
      b.Save(path, ImageFormat.Png);
    }
  }
  public static void Wheel(IntPtr h, int cx, int cy, int delta) {
    POINT p; p.X = cx; p.Y = cy; ClientToScreen(h, ref p);
    IntPtr w = (IntPtr)((delta << 16) & unchecked((int)0xFFFF0000));
    IntPtr l = (IntPtr)(((p.Y & 0xFFFF) << 16) | (p.X & 0xFFFF));
    PostMessageW(h, 0x20A, w, l);
  }
  public static string Title(IntPtr h) { var sb = new System.Text.StringBuilder(512); GetWindowTextW(h, sb, 512); return sb.ToString(); }
}
'@
[void][Drv]::SetProcessDpiAwarenessContext([IntPtr](-4))
$script:Root = Split-Path -Parent $PSScriptRoot
$script:Exe = Join-Path $Root 'build\TLAP.exe'
function Tlap-Hwnd { $p = Get-Process TLAP -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $script:Exe } | Select-Object -First 1; if ($p) { $p.Refresh(); $p.MainWindowHandle } else { [IntPtr]::Zero } }
function Tlap-Stop { Get-Process TLAP -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $script:Exe } | Stop-Process -Force; Start-Sleep -Milliseconds 300 }
function Tlap-Ini($vol = 15, $muted = 0, $repeat = 'stop') {
  $ini = "[TLAP]`r`nX=-960`r`nY=167`r`nWidth=960`r`nHeight=640`r`nVolume=$vol`r`nMuted=$muted`r`nRepeat=$repeat`r`n"
  Set-Content -Path (Join-Path $Root 'build\TLAP.ini') -Value $ini -Encoding ascii
}
function Tlap-Start($arg = $null, [switch]$noIni) {
  Tlap-Stop
  Remove-Item (Join-Path $Root 'build\TLAP_trace.log') -ErrorAction SilentlyContinue
  if (-not $noIni) { Tlap-Ini }
  if ($arg) { $script:Proc = Start-Process -FilePath $script:Exe -ArgumentList "`"$arg`"" -PassThru } else { $script:Proc = Start-Process -FilePath $script:Exe -PassThru }
  Start-Sleep -Milliseconds 700
}
function Tlap-Title { [Drv]::Title((Tlap-Hwnd)) }
function Tlap-Key([int]$vk) { $h = Tlap-Hwnd; [void][Drv]::PostMessageW($h, 0x100, [IntPtr]$vk, [IntPtr]0x00000001); [void][Drv]::PostMessageW($h, 0x101, [IntPtr]$vk, [IntPtr]([int]0xC0000001 -band 0xFFFFFFFF)) }
function Tlap-Click([int]$x, [int]$y) {   # クライアント座標（物理 px）
  $h = Tlap-Hwnd; $l = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [void][Drv]::PostMessageW($h, 0x201, [IntPtr]1, $l); Start-Sleep -Milliseconds 60
  [void][Drv]::PostMessageW($h, 0x202, [IntPtr]0, $l)
}
function Tlap-DblClick([int]$x, [int]$y) {
  $h = Tlap-Hwnd; $l = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
  [void][Drv]::PostMessageW($h, 0x201, [IntPtr]1, $l); [void][Drv]::PostMessageW($h, 0x202, [IntPtr]0, $l)
  [void][Drv]::PostMessageW($h, 0x203, [IntPtr]1, $l); [void][Drv]::PostMessageW($h, 0x202, [IntPtr]0, $l)
}
function Tlap-Move([int]$x, [int]$y) { $h = Tlap-Hwnd; [void][Drv]::PostMessageW($h, 0x200, [IntPtr]1, [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))) }
function Tlap-Up([int]$x, [int]$y) { $h = Tlap-Hwnd; [void][Drv]::PostMessageW($h, 0x202, [IntPtr]0, [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))) }
function Tlap-Down([int]$x, [int]$y) { $h = Tlap-Hwnd; [void][Drv]::PostMessageW($h, 0x201, [IntPtr]1, [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))) }
function Tlap-Cmd([int]$id) { [void][Drv]::PostMessageW((Tlap-Hwnd), 0x111, [IntPtr]$id, [IntPtr]0) }   # WM_COMMAND（メニュー項目）
function Tlap-Shot([string]$name) { $p = Join-Path $Root "build\shot_$name.png"; [Drv]::Shot((Tlap-Hwnd), $p); $p }
function Tlap-Log { Get-Content (Join-Path $Root 'build\TLAP_trace.log') -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch ' tone ' } }
function Tlap-Tones { Get-Content (Join-Path $Root 'build\TLAP_trace.log') -ErrorAction SilentlyContinue | Where-Object { $_ -match ' tone ' } }

function Tlap-Wheel([int]$x, [int]$y, [int]$delta) { [Drv]::Wheel((Tlap-Hwnd), $x, $y, $delta) }
