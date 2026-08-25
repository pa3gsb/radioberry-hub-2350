param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,

    [Parameter(Mandatory = $true)]
    [string]$OutputC,

    [Parameter(Mandatory = $true)]
    [string]$OutputH,

    [Parameter(Mandatory = $true)]
    [string]$SymbolName
)

$ErrorActionPreference = 'Stop'

$bytes = [System.IO.File]::ReadAllBytes($InputFile)
$byteCount = $bytes.Length
$inputName = [System.IO.Path]::GetFileName($InputFile)
$headerName = [System.IO.Path]::GetFileName($OutputH)
$includeGuard = (($SymbolName.ToUpperInvariant() + '_H') -replace '[^A-Z0-9_]', '_')

$outputCDir = [System.IO.Path]::GetDirectoryName($OutputC)
$outputHDir = [System.IO.Path]::GetDirectoryName($OutputH)

if ($outputCDir) {
    [System.IO.Directory]::CreateDirectory($outputCDir) | Out-Null
}

if ($outputHDir) {
    [System.IO.Directory]::CreateDirectory($outputHDir) | Out-Null
}

$header = @"
#ifndef $includeGuard
#define $includeGuard

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t ${SymbolName}_data[$byteCount];
extern const size_t ${SymbolName}_size;
extern const char ${SymbolName}_name[];

#ifdef __cplusplus
}
#endif

#endif
"@

[System.IO.File]::WriteAllText($OutputH, $header, [System.Text.UTF8Encoding]::new($false))

$builder = [System.Text.StringBuilder]::new()
[void]$builder.AppendLine("#include `"$headerName`"")
[void]$builder.AppendLine()
[void]$builder.AppendLine("const uint8_t ${SymbolName}_data[$byteCount] = {")

for ($index = 0; $index -lt $byteCount; $index++) {
    if (($index % 12) -eq 0) {
        [void]$builder.Append("    ")
    }

    [void]$builder.AppendFormat("0x{0:X2}", $bytes[$index])

    if ($index -lt ($byteCount - 1)) {
        [void]$builder.Append(", ")
    }

    if ((($index + 1) % 12) -eq 0) {
        [void]$builder.AppendLine()
    }
}

if (($byteCount % 12) -ne 0) {
    [void]$builder.AppendLine()
}

[void]$builder.AppendLine("};")
[void]$builder.AppendLine()
[void]$builder.AppendLine("const size_t ${SymbolName}_size = $byteCount;")
[void]$builder.AppendLine("const char ${SymbolName}_name[] = `"$inputName`";")

[System.IO.File]::WriteAllText($OutputC, $builder.ToString(), [System.Text.UTF8Encoding]::new($false))
