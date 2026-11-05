param(
    [string]$Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe',
    [string]$PageUrl = 'http://127.0.0.1:8765/pcm-probe.html',
    [int]$Seconds = 120,
    [switch]$PCM, # Retained for callers; all checks now use board PCM audio.
    [switch]$AM,
    [ValidateSet('','usb','lsb')][string]$SSB = '',
    [switch]$Fixture,
    [string]$OutputDir = (Join-Path $env:TEMP ('radioberry-audio-check-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$probeName = 'radioBerryPCMProbe'
$checkScript = 'verify-pcm.js'
if ($AM -and $SSB) { throw 'Choose either -AM or -SSB.' }
if ($Seconds -lt 1 -or $Seconds -gt 600) { throw 'Seconds must be between 1 and 600.' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$profileDir = Join-Path $OutputDir 'profile'
$arguments = @('--headless=new', '--mute-audio', '--no-first-run', '--no-default-browser-check', '--disable-background-networking', '--remote-debugging-port=0', ('--user-data-dir="' + $profileDir + '"'), 'about:blank')
$process = Start-Process -FilePath $Browser -ArgumentList $arguments -WindowStyle Hidden -PassThru
$socket = [Net.WebSockets.ClientWebSocket]::new()
$script:requestId = 0
$script:browserErrors = [Collections.Generic.List[string]]::new()
function Invoke-Cdp([string]$method, [hashtable]$parameters = @{}) {
    $script:requestId++
    $id = $script:requestId
    $bytes = [Text.Encoding]::UTF8.GetBytes((@{id=$id; method=$method; params=$parameters} | ConvertTo-Json -Depth 30 -Compress))
    $timeout = [Threading.CancellationTokenSource]::new(($Seconds + 60) * 1000)
    try {
        $socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text, $true, $timeout.Token).GetAwaiter().GetResult() | Out-Null
        while ($true) {
            $stream = [IO.MemoryStream]::new()
            try {
                do {
                    $buffer = [byte[]]::new(65536)
                    $received = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $timeout.Token).GetAwaiter().GetResult()
                    if ($received.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { throw 'Browser closed the debug connection.' }
                    $stream.Write($buffer, 0, $received.Count)
                } until ($received.EndOfMessage)
                $reply = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            } finally { $stream.Dispose() }
            if ($reply.method -eq 'Runtime.exceptionThrown') { $script:browserErrors.Add(($reply.params | ConvertTo-Json -Depth 12 -Compress)) }
            if ($reply.id -eq $id) {
                if ($reply.error) { throw ($reply.error | ConvertTo-Json -Compress) }
                return $reply.result
            }
        }
    } finally { $timeout.Dispose() }
}
function Invoke-Js([string]$code, [bool]$gesture = $false) {
    $reply = Invoke-Cdp 'Runtime.evaluate' @{expression=$code; awaitPromise=$true; returnByValue=$true; userGesture=$gesture}
    if ($reply.exceptionDetails) { throw ($reply.exceptionDetails | ConvertTo-Json -Depth 12 -Compress) }
    return $reply.result.value
}
try {
    $portFile = Join-Path $profileDir 'DevToolsActivePort'
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-Path -LiteralPath $portFile)) {
        if ($process.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Browser did not start.' }
        Start-Sleep -Milliseconds 100
    }
    $port = (Get-Content -LiteralPath $portFile)[0]
    $pages = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list"
    $page = $pages | Where-Object type -eq 'page' | Select-Object -First 1
    $socket.ConnectAsync([uri]$page.webSocketDebuggerUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
    Invoke-Cdp 'Page.enable' | Out-Null
    Invoke-Cdp 'Runtime.enable' | Out-Null
    Invoke-Cdp 'Network.enable' | Out-Null
    Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=1280; height=1000; deviceScaleFactor=1; mobile=$false} | Out-Null
    Invoke-Cdp 'Page.navigate' @{url=$PageUrl} | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $ready = Invoke-Js "Boolean(window.$probeName)"
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Audio page did not initialize.' }
    } until ($ready)
    Invoke-Js "window.__audioCheckSeconds = $Seconds" | Out-Null
    Invoke-Js ("window.__pcmFixture = " + $Fixture.IsPresent.ToString().ToLowerInvariant()) | Out-Null
    Invoke-Js ("window.__pcmAM = " + $AM.IsPresent.ToString().ToLowerInvariant()) | Out-Null
    Invoke-Js ("window.__pcmSSB = '" + $SSB + "'") | Out-Null
    Write-Host "Running actual audio checks for $Seconds seconds: $PageUrl"
    $checks = Invoke-Js ([IO.File]::ReadAllText((Join-Path $PSScriptRoot $checkScript))) $true
    $checks | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'audio-checks.json') -Encoding utf8
    $layouts = foreach ($width in @(1280,768,390,360,320)) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$width; height=1100; deviceScaleFactor=1; mobile=($width -lt 600)} | Out-Null
        $layout = Invoke-Js '({width:innerWidth, scrollWidth:document.documentElement.scrollWidth})'
        if ($layout.scrollWidth -gt $width) { throw "Horizontal overflow at $width px." }
        $layout
    }
    Invoke-Cdp 'Emulation.setTouchEmulationEnabled' @{enabled=$true; maxTouchPoints=1} | Out-Null
    Invoke-Js 'document.getElementById("listen").scrollIntoView({block:"center"}); new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))' | Out-Null
    $button = Invoke-Js '(() => {const r=document.getElementById("listen").getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2};})()'
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchStart';touchPoints=@(@{x=$button.x;y=$button.y})} | Out-Null
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchEnd';touchPoints=@()} | Out-Null
    Invoke-Js 'new Promise(resolve => setTimeout(resolve,300))' | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    while (-not (Invoke-Js "window.$probeName.running")) {
        if ([DateTime]::UtcNow -gt $deadline) { break }
        Invoke-Js 'new Promise(resolve => setTimeout(resolve,100))' | Out-Null
    }
    if (-not (Invoke-Js "window.$probeName.running")) { throw ('Touch Listen did not start audio: ' + ((Invoke-Js "window.$probeName.snapshot()") | ConvertTo-Json -Compress)) }
    foreach ($view in @(@{name='desktop';width=1280;height=1000;mobile=$false},@{name='mobile';width=390;height=1000;mobile=$true})) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$view.width;height=$view.height;deviceScaleFactor=1;mobile=$view.mobile} | Out-Null
        Invoke-Js 'new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))' | Out-Null
        $shot = Invoke-Cdp 'Page.captureScreenshot' @{format='png';captureBeyondViewport=$true}
        [IO.File]::WriteAllBytes((Join-Path $OutputDir ($view.name + '.png')), [Convert]::FromBase64String($shot.data))
    }
    Invoke-Js "window.$probeName.stop()" | Out-Null
    if ($script:browserErrors.Count) { throw ($script:browserErrors -join "`n") }
    $version = Invoke-Cdp 'Browser.getVersion'
    $report = @{browser=$version.product;page=$PageUrl;audio=$checks;layouts=$layouts;touch='passed';runtimeErrors=0;screenshots=$OutputDir;physicalListening='not assessed by automated check'}
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'results.json') -Encoding utf8
    $report | ConvertTo-Json -Depth 12
} catch {
    if($script:browserErrors.Count){Write-Host ($script:browserErrors -join "`n")}
    throw
} finally {
    if ($socket.State -eq [Net.WebSockets.WebSocketState]::Open) { try { Invoke-Cdp 'Browser.close' | Out-Null } catch {} }
    $socket.Dispose()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
}
