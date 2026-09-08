# M2 T4 smoke: arm the hotkey recorder with a real click, then inject
# Ctrl+Alt+T key events, then read back the persisted config.
param([int]$X = 1025, [int]$Y = 443)
$code = @"
using System; using System.Runtime.InteropServices; using System.Threading;
public class KR {
  [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]
  public struct INPUT {
    [FieldOffset(0)] public uint type;
    [FieldOffset(8)] public MI mi;
    [FieldOffset(8)] public KI ki;
  }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] i, int size);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  static int Sz() { return Marshal.SizeOf(typeof(INPUT)); }
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
"@
Add-Type -TypeDefinition $code
[KR]::Click($X, $Y)          # arm recorder (real click => window focus)
Start-Sleep -Milliseconds 500
[KR]::Key(0x11, $false)      # ctrl down
[KR]::Key(0x12, $false)      # alt down
[KR]::Key(0x54, $false)      # T down
[KR]::Key(0x54, $true)       # T up  -> recorder should capture ctrl+alt+t
[KR]::Key(0x12, $true)
[KR]::Key(0x11, $true)
Start-Sleep -Milliseconds 900
$cfg = Get-Content "$env:APPDATA\YiPie\config.json" -Raw | ConvertFrom-Json
$a = $cfg.profiles[0].actions[6]
Write-Host ("slot6: type=" + $a.type + " target=[" + $a.target + "]")
