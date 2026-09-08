# Generate src/glyph_table.generated.h from ui/glyphs.json (single source).
# Runs at build time via cmake add_custom_command; idempotent.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$json = Get-Content (Join-Path $root 'ui\glyphs.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$out = Join-Path $root 'src\glyph_table.generated.h'

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// AUTO-GENERATED from ui/glyphs.json by scripts/gen-glyph-header.ps1.')
[void]$sb.AppendLine('// DO NOT EDIT. Source of truth: ui/glyphs.json (JS side fetches the same file).')
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('#include <cstddef>')
[void]$sb.AppendLine('#include <wchar.h>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('struct GlyphEntry {')
[void]$sb.AppendLine('    const char* id;')
[void]$sb.AppendLine('    wchar_t cp;')
[void]$sb.AppendLine('    const char* const* patterns;  // NULL-terminated')
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')

$ids = $json.PSObject.Properties.Name
foreach ($id in $ids) {
  $pats = @($json.$id.patterns)
  $arr = ($pats | ForEach-Object { "`"$_`"" }) -join ", "
  if ($pats.Count -gt 0) { $arr += ", nullptr" } else { $arr = "nullptr" }
  [void]$sb.AppendLine("inline const char* kPatterns_$id[] = { $arr };")
}
[void]$sb.AppendLine('')
[void]$sb.AppendLine('inline const GlyphEntry kGlyphs[] = {')
foreach ($id in $ids) {
  $cp = $json.$id.cp
  [void]$sb.AppendLine("    { `"" + $id + "`", 0x" + $cp + ", kPatterns_" + $id + " },")
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine("inline constexpr size_t kGlyphCount = $($ids.Count);")

$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($out, $sb.ToString(), $utf8NoBom)
Write-Host "generated $out ($($ids.Count) glyphs)"
