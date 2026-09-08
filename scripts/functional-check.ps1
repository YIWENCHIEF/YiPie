# M1 close functional smoke on the NORMAL (non-probe) build.
# Verifies no ~1s stall on quick right-click after the deferred-replay fix,
# and gesture show/hide still works. SendInput injects REAL events -
# do not move the physical mouse during this run.
$code = @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
public class N {
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  static INPUT Btn(uint f) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = f; return i; }
  static INPUT Rel(int dx, int dy) { var i = new INPUT(); i.type = 0; i.mi.dwFlags = 1; i.mi.dx = dx; i.mi.dy = dy; return i; }
  public static void Click(int x, int y) {
    SetCursorPos(x, y); Thread.Sleep(50);
    SendInput(1, new[]{ Btn(0x8) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(40);
    SendInput(1, new[]{ Btn(0x10) }, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Gesture(int x, int y, int dxT, int dyT) {
    SetCursorPos(x, y); Thread.Sleep(50);
    SendInput(1, new[]{ Btn(0x8) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(20);
    for (int s = 0; s < 6; s++) { SendInput(1, new[]{ Rel(dxT/6, dyT/6) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(16); }
    Thread.Sleep(30);
    SendInput(1, new[]{ Btn(0x10) }, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
Add-Type -TypeDefinition $code

$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\yipie.exe")).Path
Start-Process $exe
Start-Sleep -Seconds 2
if (-not (Get-Process yipie -ErrorAction SilentlyContinue)) { Write-Host "FAIL: yipie not running"; exit 1 }

# 1) quick right-click: deferred replay must be instant (old bug stalled ~1000ms;
#    the harness itself already sleeps ~90ms here)
$t0 = Get-Date
[N]::Click(700, 700)
$t1 = Get-Date
$clickMs = [int]($t1 - $t0).TotalMilliseconds
Write-Host "click wall ms: $clickMs (budget incl harness 90ms sleeps: must be far below 1000)"

# 2) east gesture: cross 25px threshold -> wheel shows -> empty slot releases silently
$t0 = Get-Date
[N]::Gesture(700, 700, 130, 0)
$t1 = Get-Date
Write-Host ("gesture wall ms: " + [int]($t1 - $t0).TotalMilliseconds)
Start-Sleep -Milliseconds 300

# 3) another quick right-click: repeated use still fine
$t0 = Get-Date
[N]::Click(700, 700)
$t1 = Get-Date
Write-Host ("click2 wall ms: " + [int]($t1 - $t0).TotalMilliseconds)
Start-Sleep -Milliseconds 250

Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "=== DONE ==="
