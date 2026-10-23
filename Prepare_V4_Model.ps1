# Prepare only the SDK model inputs; this script never flashes the board.
[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$ArduinoData = (Join-Path $env:LOCALAPPDATA 'Arduino15'))
$ErrorActionPreference = 'Stop'
function Get-ModelHash([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}
$model = Join-Path $PSScriptRoot 'RTSPImageClassification\img_class_cnn.nb'
$expected = 'dc9fb06ba97685aa187f2d7eddb3d2a36824769e116d017f0b9d7c4568c1ebc6'
if ((Get-ModelHash $model) -ne $expected) {
    throw 'V4 model hash mismatch. Nothing changed.'
}
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $ArduinoData 'packages\realtek'))
$hardware = Join-Path $sdkRoot 'hardware\AmebaPro2\4.0.9-build20250805'
$nn = Join-Path $hardware 'variants\common_nn_models'
$sdkTools = Join-Path $sdkRoot 'tools\ameba_pro2_tools\1.4.5'
$targets = @(
    (Join-Path $nn 'img_class_cnn.nb'),
    (Join-Path $nn 'Cbackup_2026-10-08_img_class_cnn.nb'),
    (Join-Path $sdkTools 'img_class_cnn.nb'),
    (Join-Path $sdkTools 'Cbackup_2026-10-08_img_class_cnn.nb'),
    (Join-Path $hardware 'libraries\NeuralNetwork\examples\RTSPImageClassification\img_class_cnn.nb')
)
foreach ($target in $targets) {
    $resolved = [IO.Path]::GetFullPath($target)
    if (-not $resolved.StartsWith($sdkRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Target outside expected SDK root. Nothing changed.'
    }
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Required SDK file missing: $target. Nothing changed. Check SDK installation/version."
    }
}
$backup = Join-Path $PSScriptRoot ('backups\sdk_before_v4_' + [Guid]::NewGuid().ToString('N'))
if ($PSCmdlet.ShouldProcess($sdkRoot, 'Back up and select the bundled V4 model for all five SDK sources')) {
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $records = @()
    for ($i = 0; $i -lt $targets.Count; $i++) {
        $saved = Join-Path $backup ("model_$i.nb")
        Copy-Item -LiteralPath $targets[$i] -Destination $saved
        $records += [pscustomobject]@{original=$targets[$i]; backup=$saved}
    }
    $records | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'paths.json') -Encoding UTF8
    foreach ($target in $targets) {
        Copy-Item -LiteralPath $model -Destination $target -Force
        if ((Get-ModelHash $target) -ne $expected) {
            throw "Verification failed: $target. Backups: $backup"
        }
    }
    Write-Output "V4 selected in five SDK sources. SHA256: $expected"
    Write-Output "Backups: $backup"
    Write-Output 'Rebuild and upload the V4 sketch manually in Arduino IDE.'
}
