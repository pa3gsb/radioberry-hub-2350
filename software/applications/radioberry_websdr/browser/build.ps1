# Build one offline HTML file using only PowerShell; no downloads or npm.
$ErrorActionPreference = 'Stop'
$sourceDir = Join-Path $PSScriptRoot 'src'
$read = { param($relative) [IO.File]::ReadAllText((Join-Path $sourceDir $relative)) }
$modules = @('vendor/ubersdr/palettes.js', 'vendor/ubersdr/spectrumTrace.js', 'vendor/ubersdr/waterfallRing.js', 'board.js')
$parts = foreach ($module in $modules) {
    $source = & $read $module
    if ($source -match '(?m)^import\s') { throw "Unexpected import in $module; review the standalone bundle." }
    [regex]::Replace($source, '(?m)^export (?=(?:function|const)\s)', '')
}
$script = "(() => {`n'use strict';`n" + ($parts -join "`n") + "`n" + (& $read 'app.js') + "`n})();"
if ($script -match '(?i)</script') { throw 'A script closing tag must be escaped before embedding.' }
$html = (& $read 'index.template.html').Replace('/* INLINE_STYLE */', (& $read 'style.css')).Replace('/* INLINE_SCRIPT */', $script)
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'index.html'), $html, [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath (Join-Path $PSScriptRoot 'index.html') | Select-Object Name, Length
