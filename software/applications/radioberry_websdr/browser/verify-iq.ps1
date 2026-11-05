param(
    [string]$Python = 'python',
    [string]$Browser = 'C:\Program Files\Google\Chrome\Application\chrome.exe',
    [int]$Seconds = 20,
    [switch]$Demo,
    [string]$BoardUrl = '',
    [string]$OutputDir = (Join-Path $env:TEMP ('radioberry-iq-check-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 1 -or $Seconds -gt 120) { throw 'Seconds must be 1..120.' }
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$portFile = Join-Path $OutputDir 'server.port'
$fixture = Join-Path (Split-Path $PSScriptRoot -Parent) 'tests/host_check.py'
$server = Start-Process -FilePath $Python -ArgumentList @('-u', ('"' + $fixture + '"'), '--serve', '--port-file', ('"' + $portFile + '"')) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $OutputDir 'service.log') -RedirectStandardError (Join-Path $OutputDir 'service-errors.log') -PassThru
$process = $null
$socket = [Net.WebSockets.ClientWebSocket]::new()
$script:requestId = 0
$script:browserErrors = [Collections.Generic.List[string]]::new()
function Invoke-Cdp([string]$method, [hashtable]$parameters = @{}) {
    $script:requestId++
    $id = $script:requestId
    $bytes = [Text.Encoding]::UTF8.GetBytes((@{id=$id;method=$method;params=$parameters}|ConvertTo-Json -Depth 30 -Compress))
    $timeout = [Threading.CancellationTokenSource]::new(($Seconds+35)*1000)
    try {
        $socket.SendAsync([ArraySegment[byte]]::new($bytes),[Net.WebSockets.WebSocketMessageType]::Text,$true,$timeout.Token).GetAwaiter().GetResult()|Out-Null
        while ($true) {
            $stream = [IO.MemoryStream]::new()
            try {
                do {
                    $buffer=[byte[]]::new(65536)
                    $received=$socket.ReceiveAsync([ArraySegment[byte]]::new($buffer),$timeout.Token).GetAwaiter().GetResult()
                    if($received.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close){throw 'Browser debug connection closed.'}
                    $stream.Write($buffer,0,$received.Count)
                } until($received.EndOfMessage)
                $reply=[Text.Encoding]::UTF8.GetString($stream.ToArray())|ConvertFrom-Json
            } finally {$stream.Dispose()}
            if($reply.method -eq 'Runtime.exceptionThrown'){$script:browserErrors.Add(($reply.params|ConvertTo-Json -Depth 12 -Compress))}
            if($reply.id -eq $id){if($reply.error){throw($reply.error|ConvertTo-Json -Compress)};return $reply.result}
        }
    } finally {$timeout.Dispose()}
}
function Invoke-Js([string]$code) {
    $reply=Invoke-Cdp 'Runtime.evaluate' @{expression=$code;awaitPromise=$true;returnByValue=$true}
    if($reply.exceptionDetails){throw($reply.exceptionDetails|ConvertTo-Json -Depth 12 -Compress)}
    return $reply.result.value
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(60)
    while(-not(Test-Path -LiteralPath $portFile)) {
        if($server.HasExited -or [DateTime]::UtcNow -gt $deadline){throw('Fixture failed: '+(Get-Content -LiteralPath (Join-Path $OutputDir 'service-errors.log') -Raw))}
        Start-Sleep -Milliseconds 100
    }
    $fixturePort=(Get-Content -LiteralPath $portFile -Raw).Trim()
    $profileDir=Join-Path $OutputDir 'profile'
    $arguments=@('--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--remote-debugging-port=0',('--user-data-dir="'+$profileDir+'"'),'about:blank')
    $process=Start-Process -FilePath $Browser -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $portFileChrome=Join-Path $profileDir 'DevToolsActivePort'
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    while(-not(Test-Path -LiteralPath $portFileChrome)) {
        if($process.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'Browser did not start.'};Start-Sleep -Milliseconds 100
    }
    $port=(Get-Content -LiteralPath $portFileChrome)[0]
    $pages=Invoke-RestMethod -Uri "http://127.0.0.1:$port/json/list"
    $page=$pages|Where-Object type -eq 'page'|Select-Object -First 1
    $socket.ConnectAsync([uri]$page.webSocketDebuggerUrl,[Threading.CancellationToken]::None).GetAwaiter().GetResult()|Out-Null
    Invoke-Cdp 'Page.enable'|Out-Null;Invoke-Cdp 'Runtime.enable'|Out-Null
    Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=1280;height=1000;deviceScaleFactor=1;mobile=$false}|Out-Null
    $url="http://127.0.0.1:$fixturePort/iq-probe.html"
    if($BoardUrl){$url=$BoardUrl.TrimEnd('/')+'/iq-probe.html'}
    if($Demo){$url+='?demo=1'}
    Invoke-Cdp 'Page.navigate' @{url=$url}|Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100;$ready=Invoke-Js 'Boolean(window.radioBerryIQProbe)'
        if([DateTime]::UtcNow -gt $deadline){throw 'IQ page did not initialize.'}
    } until($ready)
    Invoke-Js "window.__iqCheckSeconds=$Seconds"|Out-Null
    Write-Host "Testing production C IQ stream in browser: $url ($Seconds seconds)"
    $checkFile=if($BoardUrl){'verify-iq-live.js'}elseif($Demo){'verify-iq-demo.js'}else{'verify-iq.js'}
    $checks=Invoke-Js ([IO.File]::ReadAllText((Join-Path $PSScriptRoot $checkFile)))
    $layouts=foreach($width in @(1280,768,390,360,320)) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$width;height=1100;deviceScaleFactor=1;mobile=($width -lt 600)}|Out-Null
        $layout=Invoke-Js '({width:innerWidth,scrollWidth:document.documentElement.scrollWidth})'
        if($layout.scrollWidth -gt $width){throw "Horizontal overflow at $width px."};$layout
    }
    Invoke-Cdp 'Emulation.setTouchEmulationEnabled' @{enabled=$true;maxTouchPoints=1}|Out-Null
    $button=Invoke-Js '(()=>{const r=document.getElementById("start").getBoundingClientRect();return{x:r.x+r.width/2,y:r.y+r.height/2};})()'
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchStart';touchPoints=@(@{x=$button.x;y=$button.y})}|Out-Null
    Invoke-Cdp 'Input.dispatchTouchEvent' @{type='touchEnd';touchPoints=@()}|Out-Null
    Invoke-Js 'new Promise(resolve=>setTimeout(resolve,1300))'|Out-Null
    if(-not(Invoke-Js 'window.radioBerryIQProbe.state.running&&window.radioBerryIQProbe.state.frames>0')){throw 'Touch did not start the IQ stream.'}
    foreach($view in @(@{name='desktop';width=1280;height=1000;mobile=$false},@{name='mobile';width=390;height=1200;mobile=$true})) {
        Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{width=$view.width;height=$view.height;deviceScaleFactor=1;mobile=$view.mobile}|Out-Null
        Invoke-Js 'new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))'|Out-Null
        $shot=Invoke-Cdp 'Page.captureScreenshot' @{format='png';captureBeyondViewport=$true}
        [IO.File]::WriteAllBytes((Join-Path $OutputDir ($view.name+'.png')),[Convert]::FromBase64String($shot.data))
    }
    Invoke-Js 'window.radioBerryIQProbe.stop()'|Out-Null
    if($script:browserErrors.Count){throw($script:browserErrors -join "`n")}
    $version=Invoke-Cdp 'Browser.getVersion'
    $report=@{browser=$version.product;page=$url;iq=$checks;layouts=$layouts;touch='passed';runtimeErrors=0;screenshots=$OutputDir;source=$(if($BoardUrl){'Physical board IQ in actual browser'}else{'Synthetic IQ packed by actual firmware C module; no RF hardware'})}
    $report|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $OutputDir 'results.json') -Encoding utf8
    $report|ConvertTo-Json -Depth 12
    if($BoardUrl -and -not $checks.passed){throw 'Physical browser IQ measurement failed; inspect results.json.'}
} finally {
    if($socket.State -eq [Net.WebSockets.WebSocketState]::Open){try{Invoke-Cdp 'Browser.close'|Out-Null}catch{}}
    $socket.Dispose()
    if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -ErrorAction SilentlyContinue}
    if(-not $server.HasExited){Stop-Process -Id $server.Id -ErrorAction SilentlyContinue}
}
