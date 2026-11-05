# Exercise the actual standalone page in headless Chrome/Edge, without packages.
param(
    [string]$Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe',
    [string]$PageUrl = '',
    [switch]$LiveBoard,
    [ValidateSet('','am','usb','lsb')][string]$FilterPreview = '',
    [string]$OutputDir = (Join-Path $env:TEMP ('radioberry-browser-check-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
if (-not $PageUrl) { & (Join-Path $PSScriptRoot 'build.ps1') | Out-Null }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$profileDir = Join-Path $OutputDir 'profile'
$arguments = @('--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--disable-background-networking', '--remote-debugging-port=0', ('--user-data-dir="' + $profileDir + '"'), 'about:blank')
$process = Start-Process -FilePath $Browser -ArgumentList $arguments -WindowStyle Hidden -PassThru
$socket = [Net.WebSockets.ClientWebSocket]::new()
$script:requestId = 0
$script:browserErrors = [Collections.Generic.List[string]]::new()
$script:networkFailures = [Collections.Generic.List[string]]::new()
function Invoke-Cdp([string]$method, [hashtable]$parameters = @{}) {
    $script:requestId++
    $id = $script:requestId
    $message = @{id=$id; method=$method; params=$parameters} | ConvertTo-Json -Depth 30 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($message)
    $timeout = [Threading.CancellationTokenSource]::new(60000)
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
            if ($reply.method -eq 'Network.loadingFailed') { $script:networkFailures.Add(($reply.params | ConvertTo-Json -Depth 12 -Compress)) }
            if ($reply.id -eq $id) {
                if ($reply.error) { throw ($reply.error | ConvertTo-Json -Compress) }
                return $reply.result
            }
        }
    } finally { $timeout.Dispose() }
}
function Invoke-Js([string]$code) {
    $reply = Invoke-Cdp 'Runtime.evaluate' @{expression=$code; awaitPromise=$true; returnByValue=$true}
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
    if (-not $PageUrl) { Invoke-Cdp 'Network.setBlockedURLs' @{urls=@('http://*', 'https://*')} | Out-Null }
    Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=1280; height=1000; deviceScaleFactor=1; mobile=$false} | Out-Null
    # Test-only clock and row capture, installed before the unmodified app loads.
    $instrument = @'
window.__probe = { tick: null, rows: [] };
const originalInterval = window.setInterval;
window.setInterval = function(fn, ms, ...args) {
  if (ms === 100) { window.__probe.tick = () => fn(...args); return 1; }
  return originalInterval(fn, ms, ...args);
};
const originalPut = CanvasRenderingContext2D.prototype.putImageData;
CanvasRenderingContext2D.prototype.putImageData = function(image, ...args) {
  if (image.width === 512 && image.height === 1) window.__probe.rows.push(Array.from(image.data));
  return originalPut.call(this, image, ...args);
};
'@
    Invoke-Cdp 'Page.addScriptToEvaluateOnNewDocument' @{source=$instrument} | Out-Null
    $url = if ($PageUrl) { $PageUrl } else { ([uri](Join-Path $PSScriptRoot 'index.html')).AbsoluteUri }
    Invoke-Cdp 'Page.navigate' @{url=$url} | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $ready = if ($PageUrl) { Invoke-Js 'Boolean(window.__probe && window.__probe.rows.length)' } else { Invoke-Js 'Boolean(window.__probe && window.__probe.tick)' }
        if ([DateTime]::UtcNow -gt $deadline) {
            $state = Invoke-Js '({url:location.href, ready:document.readyState, state:document.getElementById("run-state")?.textContent, rows:window.__probe?.rows.length, pcm:!!window.radioBerryPCMProbe, resources:performance.getEntriesByType("resource").map(r=>({name:r.name,duration:r.duration})), scripts:Array.from(document.scripts,s=>({src:s.src,length:s.textContent.length}))})'
            throw ('Application did not initialize: ' + ($state | ConvertTo-Json -Compress) +
                ' Runtime errors: ' + ($script:browserErrors -join ' ') +
                ' Network failures: ' + ($script:networkFailures -join ' '))
        }
    } until ($ready)
    $checkFile = if ($LiveBoard) { 'verify-live.js' } elseif ($PageUrl) { 'verify-board.js' } else { 'verify.js' }
    $checks = Invoke-Js ([IO.File]::ReadAllText((Join-Path $PSScriptRoot $checkFile)))
    if ($PageUrl) {
        Invoke-Js 'document.getElementById("pause").click()' | Out-Null
        $rowsBeforeResize = Invoke-Js 'window.__probe.rows.length'
    }
    $rowsBeforeResize = Invoke-Js 'window.__probe.rows.length'
    $layouts = foreach ($width in @(1280, 768, 390, 360, 320)) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$width; height=1100; deviceScaleFactor=2; mobile=($width -lt 600)} | Out-Null
        Invoke-Js 'new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))' | Out-Null
        $layout = Invoke-Js '({width: innerWidth, scrollWidth: document.documentElement.scrollWidth, spectrumWidth: document.getElementById("spectrum").getBoundingClientRect().width})'
        if ($layout.scrollWidth -gt $width) { throw "Horizontal overflow at $width px." }
        if ((Invoke-Js 'window.__probe.rows.length') -ne $rowsBeforeResize) { throw 'Resizing added a duplicate waterfall row.' }
        $layout
    }
    if (-not $LiveBoard) {
    Invoke-Cdp 'Emulation.setTouchEmulationEnabled' @{enabled=$true; maxTouchPoints=1} | Out-Null
    $button = Invoke-Js '(() => { const b = document.getElementById("step-up").getBoundingClientRect(); return {x:b.x+b.width/2,y:b.y+b.height/2}; })()'
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchStart'; touchPoints=@(@{x=$button.x; y=$button.y})} | Out-Null
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchEnd'; touchPoints=@()} | Out-Null
    $touchFrequency = Invoke-Js 'document.getElementById("frequency").value'
    if ($PageUrl) {
        Invoke-Js 'document.getElementById("pause").click(); new Promise(resolve => { const t=setInterval(() => { if(document.getElementById("frequency").value === "7101.000") {clearInterval(t); resolve();}}, 50); setTimeout(() => {clearInterval(t); resolve();}, 5000); })' | Out-Null
        $touchFrequency = Invoke-Js 'document.getElementById("frequency").value'
    }
    if ($touchFrequency -ne '7101.000') { throw "Touch step failed: $touchFrequency" }
    $tap = Invoke-Js '(() => { const r = document.getElementById("spectrum").getBoundingClientRect(); const left = r.width < 550 ? 48 : 56; return {x:r.x+left+(r.width-left-18)*.75, y:r.y+60, tolerance:48000/(r.width-left-18)}; })()'
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchStart'; touchPoints=@(@{x=$tap.x; y=$tap.y})} | Out-Null
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchEnd'; touchPoints=@()} | Out-Null
    if ($PageUrl) { Invoke-Js 'new Promise(resolve => setTimeout(resolve, 700))' | Out-Null }
    $tapHz = Invoke-Js 'Number(document.getElementById("frequency").value.replace(",", ".")) * 1000'
    $expectedTapHz = if ($PageUrl) { 7113000 - 46.875 } else { 7113000 }
    if ([Math]::Abs($tapHz - $expectedTapHz) -gt $tap.tolerance) { throw "Touch spectrum tuning failed: $tapHz" }
    if ($PageUrl) {
        Invoke-Js 'document.getElementById("reset").click(); new Promise(resolve => setTimeout(resolve, 700))' | Out-Null
    } else {
        Invoke-Js 'document.getElementById("reset").click(); for(let i=0;i<205;i++) window.__probe.tick();' | Out-Null
    }
    }
    if ($LiveBoard) {
        Invoke-Js 'document.getElementById("pause").click(); new Promise(resolve => setTimeout(resolve, 500))' | Out-Null
    }
    if ($FilterPreview) {
        Invoke-Js ("document.getElementById('audio-mode').value='" + $FilterPreview + "'; document.getElementById('audio-mode').dispatchEvent(new Event('change',{bubbles:true}));") | Out-Null
    }
    foreach ($view in @(@{name='desktop'; width=1280; height=1000; mobile=$false}, @{name='mobile'; width=390; height=1250; mobile=$true})) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$view.width; height=$view.height; deviceScaleFactor=1; mobile=$view.mobile} | Out-Null
        Invoke-Js 'new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))' | Out-Null
        $shot = Invoke-Cdp 'Page.captureScreenshot' @{format='png'; captureBeyondViewport=$true}
        [IO.File]::WriteAllBytes((Join-Path $OutputDir ($view.name + '.png')), [Convert]::FromBase64String($shot.data))
    }
    if ($script:browserErrors.Count) { throw ($script:browserErrors -join "`n") }
    $version = Invoke-Cdp 'Browser.getVersion'
    $htmlBytes = if ($PageUrl) { Invoke-Js 'new TextEncoder().encode(document.documentElement.outerHTML).length' } else { (Get-Item (Join-Path $PSScriptRoot 'index.html')).Length }
    $report = @{browser=$version.product; checks=$checks; layouts=$layouts; touch=$(if($LiveBoard){'not exercised'}else{'passed'}); runtimeErrors=0; page=$url; externalRequests=$(if($PageUrl){'same origin checked'}else{'blocked'}); screenshots=$OutputDir; htmlBytes=$htmlBytes}
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDir 'results.json') -Encoding utf8
    $report | ConvertTo-Json -Depth 12
} finally {
    if ($socket.State -eq [Net.WebSockets.WebSocketState]::Open) {
        try { Invoke-Cdp 'Browser.close' | Out-Null } catch { }
    }
    $socket.Dispose()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
}
