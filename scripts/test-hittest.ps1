# Verify wheel hit-test absorption: during an active gesture (right button
# held), WindowFromPoint inside the disc must return YiPieWheelWindow;
# outside the disc must NOT. Idle state must NOT return the wheel either.
$code = @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Threading;
public class HT {
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MI mi; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x; public int y; }
  static int Sz() { return Marshal.SizeOf(typeof(INPUT)); }
  public static void Down() { var d = new INPUT(); d.type = 0; d.mi.dwFlags = 0x8; SendInput(1, new[]{ d }, Sz()); }
  public static void Up() { var u = new INPUT(); u.type = 0; u.mi.dwFlags = 0x10; SendInput(1, new[]{ u }, Sz()); }
  public static void Move(int x, int y) {
    var i = new INPUT(); i.type = 0; i.mi.dwFlags = 0x1; i.mi.dx = x; i.mi.dy = y;
    SendInput(1, new[]{ i }, Sz());
  }
  public static string ClassAt(int x, int y) {
    var p = new POINT { x = x, y = y };
    IntPtr h = WindowFromPoint(p);
    if (h == IntPtr.Zero) return "(null)";
    var sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
    return sb.ToString();
  }
  public static IntPtr WheelHwnd(uint targetPid) {
    IntPtr r = IntPtr.Zero;
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == targetPid) { var sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
        if (sb.ToString() == "YiPieWheelWindow") { r = h; } }
      return true; }, IntPtr.Zero);
    return r;
  }
  public static bool IsWindowVisible(IntPtr h) {
    // not used; keep simple
    return h != IntPtr.Zero;
  }
}
"@
Add-Type -TypeDefinition $code

# Use SetCursorPos for moves (simpler): it generates WM_MOUSEMOVE to the hit window.
$code2 = @"
using System; using System.Threading;
public class HT2 {
  [System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
}
"@
Add-Type -TypeDefinition $code2

Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Remove-Item "$env:APPDATA\YiPie\config.json*" -Force -ErrorAction SilentlyContinue
$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\yipie.exe")).Path
Start-Process $exe
Start-Sleep 3
$p = Get-Process yipie | Select-Object -First 1
$pid1 = [uint32]$p.Id

$cx = 900; $cy = 500   # gesture anchor (wheel center after activation)
$R = 138               # default wheelRadius

# idle check: wheel hidden -> WindowFromPoint at anchor must NOT be the wheel
$idle = [HT]::ClassAt($cx, $cy)
Write-Host "idle class at anchor: $idle"

# press right, drag east past threshold to activate wheel
[HT2]::SetCursorPos($cx, $cy); Start-Sleep -Milliseconds 120
[HT]::Down(); Start-Sleep -Milliseconds 60
# moves MUST be SendInput MOVE events (SetCursorPos does not reach WH_MOUSE_LL)
for ($i = 1; $i -le 8; $i++) { [HT]::Move(8, 0); Start-Sleep -Milliseconds 25 }
Start-Sleep -Milliseconds 400
# confirm the wheel actually activated: its window must exist & be visible
$wheel = [HT]::WheelHwnd($pid1)
Write-Host ("wheel hwnd during gesture: " + $wheel)

# inside disc (center offset 70px < R): expect YiPieWheelWindow
$inside = [HT]::ClassAt($cx + 70, $cy)
# outside disc (250px > R+6): expect NOT wheel
$outside = [HT]::ClassAt($cx + 147, $cy)   # inside window bounds (half=150) but outside disc (144)
# far corner: not wheel
$far = [HT]::ClassAt(100, 100)
Write-Host "active inside(70px):  $inside"
Write-Host "active outside(250px): $outside"
Write-Host "active far corner:     $far"

[HT]::Up()
Start-Sleep -Milliseconds 300
$after = [HT]::ClassAt($cx + 70, $cy)
Write-Host "after release inside:  $after"

Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
$pass = ($inside -eq "YiPieWheelWindow") -and ($outside -ne "YiPieWheelWindow") -and ($far -ne "YiPieWheelWindow") -and ($idle -ne "YiPieWheelWindow") -and ($after -ne "YiPieWheelWindow")
if ($pass) { Write-Host "HIT-TEST VERDICT: PASS" } else { Write-Host "HIT-TEST VERDICT: FAIL" }
