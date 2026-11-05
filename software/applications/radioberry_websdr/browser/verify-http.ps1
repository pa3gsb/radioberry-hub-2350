# Exercise the browser over HTTP against the actual firmware C service on localhost.
param(
    [string]$Python = 'python',
    [string]$Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe',
    [string]$OutputDir = (Join-Path $env:TEMP ('radioberry-http-check-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$portFile = Join-Path $OutputDir 'server.port'
$stdout = Join-Path $OutputDir 'service.log'
$stderr = Join-Path $OutputDir 'service-errors.log'
$fixture = Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/host_check.py'
$server = Start-Process -FilePath $Python -ArgumentList @('-u', ('"' + $fixture + '"'), '--serve', '--port-file', ('"' + $portFile + '"')) -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while (-not (Test-Path -LiteralPath $portFile)) {
        if ($server.HasExited -or [DateTime]::UtcNow -gt $deadline) {
            throw ('C service fixture failed: ' + (Get-Content -LiteralPath $stderr -Raw))
        }
        Start-Sleep -Milliseconds 100
    }
    $port = (Get-Content -LiteralPath $portFile -Raw).Trim()
    & (Join-Path $PSScriptRoot 'verify.ps1') -Browser $Browser -PageUrl "http://127.0.0.1:$port/" -OutputDir (Join-Path $OutputDir 'browser')
    Write-Output "C service and transport results: $stdout"
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -ErrorAction SilentlyContinue }
}
