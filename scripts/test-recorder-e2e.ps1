# M2 T4 full recorder E2E: fresh app + wiped cache -> open settings ->
# click north sector -> arm recorder -> inject Ctrl+Alt+T -> read config.
$ErrorActionPreference = 'Stop'
$src = @'
using System; using System.Text; using System.Runtime.InteropServices; using System.Threading;
public class H {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]
  public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public MI mi; [FieldOffset(8)] public KI ki; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  static int Sz() { return Marshal.SizeOf(typeof(INPUT)); }
  public static IntPtr ByClass(uint targetPid, string cls) {
    IntPtr r = IntPtr.Zero;
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == targetPid) { var sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
        if (sb.ToString() == cls) r = h; }
      return true; }, IntPtr.Zero);
    return r;
  }
  public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  public static void Click(int x, int y) {
    SetCursorPos(x, y); Thread.Sleep(120);
    var d = new INPUT(); d.type = 0; d.mi.dwFlags = 0x2;
    var u = new INPUT(); u.type = 0; u.mi.dwFlags = 0x4;
    SendInput(1, new[]{ d }, Sz()); Thread.Sleep(80);
    SendInput(1, new[]{ u }, Sz());
  }
  public static void Key(ushort vk, bool up) {
    var i = new INPUT(); i.type = 1; i.ki.wVk = vk; i.ki.dwFlags = up ? (uint)2 : (uint)0;
    SendInput(1, new[]{ i }, Sz()); Thread.Sleep(60);
  }
}
'@
Add-Type -TypeDefinition $src

# fresh app
Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 1
Remove-Item "$env:LOCALAPPDATA\YiPie\EBWebView" -Recurse -Force -ErrorAction SilentlyContinue
$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\yipie.exe")).Path
Start-Process $exe
Start-Sleep 3
$p = Get-Process yipie | Select-Object -First 1
$pid1 = [uint32]$p.Id

# open settings via tray double-click
$tray = [H]::ByClass($pid1, 'YiPieTrayWindow')
[H]::PostMessageW($tray, 0x8001, [IntPtr]1, [IntPtr]0x203) | Out-Null
Start-Sleep 5
$st = [H]::ByClass($pid1, 'YiPieSettingsWindow')
if ($st -eq [IntPtr]::Zero) { Write-Host 'FAIL: no settings window'; exit 1 }
$rc = [H]::Rect($st)
Write-Host ("settings at " + $rc.L + "," + $rc.T)

# click north sector (slot 6): window-local (387,222)
[H]::Click(($rc.L + 387), ($rc.T + 222))
Start-Sleep -Milliseconds 600
# arm recorder: window-local (822,214)
[H]::Click(($rc.L + 822), ($rc.T + 214))
Start-Sleep -Milliseconds 500
# inject Ctrl+Alt+T
[H]::Key(0x11, $false); [H]::Key(0x12, $false)
[H]::Key(0x54, $false); [H]::Key(0x54, $true)
[H]::Key(0x12, $true);  [H]::Key(0x11, $true)
Start-Sleep -Milliseconds 900

$cfg = Get-Content "$env:APPDATA\YiPie\config.json" -Raw | ConvertFrom-Json
$a = $cfg.profiles[0].actions[6]
Write-Host ("slot6: type=" + $a.type + " target=[" + $a.target + "]")
# leave app running for AX title inspection
