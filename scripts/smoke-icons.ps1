# M3c T2 smoke: slot0 = launch calc.exe -> gesture -> screenshot wheel.
# Expect: calculator exe icon rendered in east sector (bitmap path),
# other empty sectors show + placeholder.
$ErrorActionPreference = 'Stop'
$cfgPath = Join-Path $env:APPDATA "YiPie\config.json"
$backup = "$cfgPath.m3c-backup"
if (Test-Path $cfgPath) { Copy-Item $cfgPath $backup -Force }

$testCfg = @'
{
  "gesture": { "triggerButton": "right", "dragThreshold": 25.0, "coreRadius": 50.0,
    "outerEscape": true, "outerEscapeDistance": 300.0, "disableOnModifier": false,
    "disableOnFullScreen": false, "autoStart": false, "isolationMode": "blacklist",
    "whitelistProcesses": [], "blacklistProcesses": [] },
  "appearance": { "shape": "classic", "theme": "dark", "wheelRadius": 138.0,
    "innerRadius": 52.0, "showLabels": true, "opacity": "mid", "language": "auto",
    "subWheelWidth": 56.0 },
  "profiles": [ { "processName": "Global", "sectorCount": 8, "actions": [
    { "type": "launch", "name": "", "target": "C:\\Windows\\System32\\calc.exe", "args": "", "iconKey": "", "subActions": [] },
    { "type": "hotkey", "name": "", "target": "ctrl+c", "args": "", "iconKey": "", "subActions": [] },
    { "type": "launch", "name": "", "target": "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", "args": "", "iconKey": "", "subActions": [] },
    { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
    { "type": "launch", "name": "", "target": "C:\\no\\such_app.exe", "args": "", "iconKey": "", "subActions": [] },
    { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
    { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
    { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] }
  ] } ]
}
'@
[System.IO.File]::WriteAllText($cfgPath, $testCfg, (New-Object System.Text.UTF8Encoding $false))

$code = @"
using System; using System.Runtime.InteropServices; using System.Threading;
using System.Drawing;
public class S3 {
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MI mi; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  static int Sz() { return Marshal.SizeOf(typeof(INPUT)); }
  static void Btn(uint f) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = f; SendInput(1, new[]{ i }, Sz()); }
  static void Move(int dx, int dy) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = 1; i.mi.dx = dx; i.mi.dy = dy; SendInput(1, new[]{ i }, Sz()); }
  public static void DragHold(int x, int y, int dxTotal) {
    SetCursorPos(x, y); Thread.Sleep(150);
    Btn(0x8); Thread.Sleep(60);
    for (int i = 0; i < 8; i++) { Move(dxTotal / 8, 0); Thread.Sleep(25); }
    Thread.Sleep(500);
  }
  public static void Release() { Btn(0x10); }
  public static void Shot(string path, int x, int y, int w, int h) {
    using (var bmp = new Bitmap(w, h)) {
      using (var g = Graphics.FromImage(bmp)) { g.CopyFromScreen(x, y, 0, 0, new Size(w, h)); }
      bmp.Save(path);
    }
  }
}
"@
Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing

Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\yipie.exe")).Path
Start-Process $exe
Start-Sleep 3

$cx = 960; $cy = 540
[S3]::DragHold($cx, $cy, 150)
[S3]::Shot((Join-Path $env:TEMP "m3c-icons-wheel.png"), ($cx - 240), ($cy - 240), 480, 480)
Start-Sleep -Milliseconds 200
[S3]::Release()
Start-Sleep -Milliseconds 300

if (Test-Path $backup) { Move-Item $backup $cfgPath -Force }
Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host ("shot: " + (Join-Path $env:TEMP "m3c-icons-wheel.png"))
