# M3b T2 smoke: inject temp config with 3 sub-actions on east sector,
# perform a synthetic right-drag into the sub-ring zone, screenshot it.
$ErrorActionPreference = 'Stop'
$cfgPath = Join-Path $env:APPDATA "YiPie\config.json"
$backup = "$cfgPath.m3b-backup"
if (Test-Path $cfgPath) { Copy-Item $cfgPath $backup -Force }

# build test config JSON (UTF-8 no BOM)
$testCfg = @'
{
  "gesture": {
    "triggerButton": "right",
    "dragThreshold": 25.0,
    "coreRadius": 50.0,
    "outerEscape": true,
    "outerEscapeDistance": 300.0,
    "disableOnModifier": false,
    "disableOnFullScreen": false,
    "autoStart": false,
    "isolationMode": "blacklist",
    "whitelistProcesses": [],
    "blacklistProcesses": []
  },
  "appearance": {
    "shape": "classic",
    "theme": "dark",
    "wheelRadius": 138.0,
    "innerRadius": 52.0,
    "showLabels": true,
    "opacity": "mid",
    "language": "auto",
    "subWheelWidth": 56.0
  },
  "profiles": [
    {
      "processName": "Global",
      "sectorCount": 8,
      "actions": [
        { "type": "launch", "name": "计算器", "target": "calc.exe", "args": "", "iconKey": "",
          "subActions": [
            { "type": "hotkey", "name": "复制", "target": "ctrl+c", "args": "", "iconKey": "", "subActions": [] },
            { "type": "hotkey", "name": "粘贴", "target": "ctrl+v", "args": "", "iconKey": "", "subActions": [] },
            { "type": "hotkey", "name": "截图", "target": "win+shift+s", "args": "", "iconKey": "", "subActions": [] }
          ] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "", "subActions": [] }
      ]
    }
  ]
}
'@
[System.IO.File]::WriteAllText($cfgPath, $testCfg, (New-Object System.Text.UTF8Encoding $false))

$code = @"
using System; using System.Runtime.InteropServices; using System.Threading;
using System.Drawing;
public class SM {
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MI mi; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  static int Sz() { return Marshal.SizeOf(typeof(INPUT)); }
  static void Btn(uint f) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = f; SendInput(1, new[]{ i }, Sz()); }
  static void Move(int dx, int dy) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = 1; i.mi.dx = dx; i.mi.dy = dy; SendInput(1, new[]{ i }, Sz()); }
  public static void DragHold(int x, int y, int dxTotal) {
    SetCursorPos(x, y); Thread.Sleep(150);
    Btn(0x8); Thread.Sleep(60);                       // right down
    for (int i = 0; i < 8; i++) { Move(dxTotal / 8, 0); Thread.Sleep(25); }  // east ~150px
    Thread.Sleep(500);
  }
  public static void Release() { Btn(0x10); }         // right up
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

$cx = 900; $cy = 500
[SM]::DragHold($cx, $cy, 150)
[SM]::Shot((Join-Path $env:TEMP "m3b-subring-open.png"), ($cx - 230), ($cy - 230), 460, 460)
Start-Sleep -Milliseconds 200
[SM]::Release()
Start-Sleep -Milliseconds 300

# restore config
if (Test-Path $backup) { Move-Item $backup $cfgPath -Force }
Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host ("shot: " + (Join-Path $env:TEMP "m3b-subring-open.png"))
