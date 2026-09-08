# One-time fetch of the WebView2 SDK (nuget package Microsoft.Web.WebView2)
# into third_party/webview2/{include,x64}. Idempotent: skips when marker exists.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dst = Join-Path $root 'third_party\webview2'
$marker = Join-Path $dst '.version'

if (Test-Path $marker) {
    Write-Host "webview2 SDK already present: $(Get-Content $marker)"
    exit 0
}

$pkgUrl = 'https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2'
$tmp = Join-Path $env:TEMP 'yipie-webview2.nupkg.zip'
Invoke-WebRequest -Uri $pkgUrl -OutFile $tmp -UseBasicParsing

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($tmp)
try {
    $want = @(
        'build/native/include/WebView2.h',
        'build/native/include/WebView2EnvironmentOptions.h',
        'build/native/x64/WebView2LoaderStatic.lib'
    )
    foreach ($e in $zip.Entries) {
        if ($want -contains $e.FullName) {
            $out = if ($e.FullName -like 'build/native/include/*') {
                Join-Path $dst 'include' } else { Join-Path $dst 'x64' }
            New-Item -ItemType Directory -Force $out | Out-Null
            $target = Join-Path $out $e.Name
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $target, $true)
            Write-Host "extracted $target"
        }
    }
    # record version from the nuspec entry name
    $nuspec = $zip.Entries | Where-Object { $_.FullName -like '*.nuspec' } | Select-Object -First 1
    if ($nuspec) {
        $sr = New-Object System.IO.StreamReader $nuspec.Open()
        $xmlText = $sr.ReadToEnd(); $sr.Close()
        $ver = [regex]::Match($xmlText, '<version>([^<]+)</version>').Groups[1].Value
        Set-Content $marker $ver
        Write-Host "webview2 SDK version: $ver"
    }
} finally {
    $zip.Dispose()
    Remove-Item $tmp -ErrorAction SilentlyContinue
}
