# M2 T4 smoke: real left-click at screen coords (for WebView2 content testing).
# Usage: powershell -File click-at.ps1 -X 691 -Y 448
param([int]$X, [int]$Y)
$code = @"
using System; using System.Runtime.InteropServices; using System.Threading;
public class C {
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
  static INPUT Btn(uint f) { var v = new INPUT(); v.type = 0; v.mi.dwFlags = f; return v; }
  public static void Click(int x, int y) {
    SetCursorPos(x, y); Thread.Sleep(80);
    SendInput(1, new[]{ Btn(0x2) }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(60);
    SendInput(1, new[]{ Btn(0x4) }, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
Add-Type -TypeDefinition $code
[C]::Click($X, $Y)
Start-Sleep -Milliseconds 400
Write-Host "clicked ($X,$Y)"
