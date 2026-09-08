$code = @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
public class M {
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)]
  public struct POINT { public int x; public int y; }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  const uint INPUT_MOUSE = 0, MOVE = 0x0001, RIGHTDOWN = 0x0008, RIGHTUP = 0x0010;
  static INPUT Btn(uint f) { var i = new INPUT(); i.type = INPUT_MOUSE; i.mi.dwFlags = f; return i; }
  static INPUT Rel(int dx, int dy) { var i = new INPUT(); i.type = INPUT_MOUSE; i.mi.dwFlags = MOVE; i.mi.dx = dx; i.mi.dy = dy; return i; }
  static POINT P;
  public static void Click(int x, int y) {
    SetCursorPos(x, y); GetCursorPos(out P);
    uint d = SendInput(1, new[]{ Btn(RIGHTDOWN) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(30);
    uint u = SendInput(1, new[]{ Btn(RIGHTUP) }, Marshal.SizeOf(typeof(INPUT)));
    Console.WriteLine("click at=(" + P.x + "," + P.y + ") down=" + d + " up=" + u);
  }
  public static void Gesture(int x, int y, int dxTotal, int dyTotal) {
    SetCursorPos(x, y); GetCursorPos(out P);
    uint d = SendInput(1, new[]{ Btn(RIGHTDOWN) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(20);
    int sx = dxTotal / 6, sy = dyTotal / 6;
    for (int s = 0; s < 6; s++) {
      SendInput(1, new[]{ Rel(sx, sy) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(16);
    }
    GetCursorPos(out P);
    Thread.Sleep(30);
    uint u = SendInput(1, new[]{ Btn(RIGHTUP) }, Marshal.SizeOf(typeof(INPUT)));
    Console.WriteLine("gest down=" + d + " end=(" + P.x + "," + P.y + ") up=" + u);
  }
}
"@
Add-Type -TypeDefinition $code

$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build-probe\yipie.exe")).Path
Start-Process $exe
Start-Sleep -Seconds 2

$proc = Get-Process yipie -ErrorAction SilentlyContinue
if (-not $proc) { Write-Host "FAIL: yipie not running"; exit 1 }

for ($i = 0; $i -lt 8; $i++) {
  [M]::Click(700, 700)
  Start-Sleep -Milliseconds 350
}
[M]::Click(700, 700)   # dismiss any context menu
Start-Sleep -Milliseconds 300

for ($i = 0; $i -lt 8; $i++) {
  [M]::Gesture(700, 700, 120, 0)   # 120px east, > dragThreshold(25), < escape(186)
  Start-Sleep -Milliseconds 400
}
Start-Sleep -Milliseconds 300

$log = Join-Path $env:TEMP "yipie_latency.log"
if (Test-Path $log) { Write-Host "=== LOG ==="; Get-Content $log } else { Write-Host "FAIL: no log at $log" }

Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "=== DONE ==="
