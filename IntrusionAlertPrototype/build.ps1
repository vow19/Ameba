param(
    [string]$SourceData = (Join-Path $env:LOCALAPPDATA 'Arduino15')
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
# 在碰觸 SDK 前檢查未含跳脫字元的 WPA2 密碼常值，絕不輸出密碼內容。
$sketchText = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $projectDir 'WiFiCredentials.h')
$passwordLiteral = [regex]::Match($sketchText, 'char\s+pass\[\]\s*=\s*"((?:\\.|[^"\\])*)"\s*;')
if ($passwordLiteral.Success -and -not $passwordLiteral.Groups[1].Value.Contains('\')) {
    $passwordValue = $passwordLiteral.Groups[1].Value
    $passwordBytes = [Text.Encoding]::UTF8.GetByteCount($passwordValue)
    $validPassphrase = $passwordBytes -ge 8 -and $passwordBytes -le 63
    $validHexKey = $passwordValue -cmatch '^[0-9a-fA-F]{64}$'
    if (-not ($validPassphrase -or $validHexKey)) {
        throw 'Invalid WPA2 credential length. Enter the actual Wi-Fi password locally; do not pad it. No build or upload was started.'
    }
}
# 複雜 C++ 常值不在此猜測解碼；這個檢查也無法證明密碼符合基地台設定。
$storageDir = Join-Path (Split-Path -Parent $projectDir) '.amb82'
$environmentDir = $storageDir
# 官方模型工具以 portable 關鍵字辨識獨立套件，否則會回到全域 Arduino15。
$dataDir = Join-Path $environmentDir 'portable'
$buildDir = Join-Path $storageDir 'intrusion-build'
$logsDir = Join-Path $storageDir 'intrusion-logs'
$configFile = Join-Path $environmentDir 'arduino-cli.yaml'
$readyFile = Join-Path $environmentDir 'copy-complete.txt'
$fqbn = 'realtek:AmebaPro2:Ameba_AMB82-MINI'
$boardOptions = '01_AutoUploadMode=Disable,02_UploadBaudrate=2M,03_EraseFlash=Disable,04_CameraOption=f37,05_FCSMode=Disable,06_LoadNNModelSource=flash,07_SHOWNNLOGS=Disable,08_OTAMode=Disable,09_StdLibInit=Enable'

Get-Command arduino-cli -ErrorAction Stop | Out-Null
New-Item -ItemType Directory -Force -Path $storageDir | Out-Null
Set-Content -LiteralPath (Join-Path $storageDir '.gitignore') -Value '*' -Encoding ASCII

# 官方建置工具會寫入套件目錄；使用專案內副本保護全域 SDK。
if (-not (Test-Path -LiteralPath $readyFile)) {
    $installedCore = Join-Path $SourceData 'packages\realtek\hardware\AmebaPro2\4.0.9-build20250805'
    if (-not (Test-Path -LiteralPath $installedCore)) {
        throw "Required core is missing: $installedCore"
    }
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    Write-Output 'Copying installed Arduino packages into the project build environment...'
    # Robocopy 支援套件內超過傳統 MAX_PATH 的範例路徑；不使用刪除或鏡像選項。
    & robocopy (Join-Path $SourceData 'packages') (Join-Path $dataDir 'packages') /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { throw "Package copy failed: robocopy exit $LASTEXITCODE" }
    Get-ChildItem -LiteralPath $SourceData -File | Where-Object {
        $_.Name -match '^(package.*_index|library_index)\.json(\.sig)?$'
    } | Copy-Item -Destination $dataDir -Force
    "Source: $SourceData`r`nCopied: $(Get-Date -Format o)" | Set-Content -LiteralPath $readyFile -Encoding UTF8
}

New-Item -ItemType Directory -Force -Path $buildDir,$logsDir,(Join-Path $environmentDir 'downloads'),(Join-Path $environmentDir 'sketchbook') | Out-Null
# YAML 使用單引號保留 Windows 路徑；不修改使用者的全域 CLI 設定。
$yamlData = $dataDir.Replace("'", "''")
$yamlDownloads = (Join-Path $environmentDir 'downloads').Replace("'", "''")
$yamlUser = (Join-Path $environmentDir 'sketchbook').Replace("'", "''")
@"
board_manager:
  additional_urls:
    - https://github.com/Ameba-AIoT/ameba-arduino-pro2/raw/main/Arduino_package/package_realtek_amebapro2_index.json
directories:
  data: '$yamlData'
  downloads: '$yamlDownloads'
  user: '$yamlUser'
"@ | Set-Content -LiteralPath $configFile -Encoding UTF8

# 僅編譯，不指定 COM Port，也不傳入 --upload。
$logFile = Join-Path $logsDir ('compile-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$startedAt = Get-Date
# 全域 Core 可由使用者升級；編譯仍使用工作區固定版本，保護目前所有全域模型清單。
function Get-GlobalManifestState {
    $pattern = Join-Path $SourceData 'packages\realtek\hardware\AmebaPro2\*\variants\common_nn_models\amebapro2_fwfs_nn_models.json'
    $files = @(Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Sort-Object FullName)
    return (($files | ForEach-Object { $_.FullName + ':' + (Get-FileHash -LiteralPath $_.FullName).Hash }) -join "`n")
}
$globalManifestBefore = Get-GlobalManifestState
# 舊版 Windows SDK 工具無法使用 C.UTF-8；只改本程序環境並於結束後還原。
$previousLocale = @{}
foreach ($localeKey in @('LANG', 'LC_ALL', 'LC_CTYPE')) {
    $previousLocale[$localeKey] = [Environment]::GetEnvironmentVariable($localeKey, 'Process')
    [Environment]::SetEnvironmentVariable($localeKey, 'C', 'Process')
}
try {
    # 官方 include 清單很長；由 CLI 展開原設定後放入 GCC response file，縮短 Windows 命令列。
    $properties = & arduino-cli --config-file $configFile compile --fqbn $fqbn --board-options $boardOptions --show-properties=expanded $projectDir
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read Arduino build properties.' }
    $includeLine = $properties | Where-Object { $_.StartsWith('compiler.ameba.c.include=') } | Select-Object -First 1
    if (-not $includeLine) { throw 'SDK include property was not found.' }
    $responsePath = Join-Path $environmentDir 'intrusion-includes.rsp'
    $includeFlags = $includeLine.Substring('compiler.ameba.c.include='.Length).Replace('\', '/')
    [IO.File]::WriteAllText($responsePath, $includeFlags, (New-Object Text.UTF8Encoding($false)))
    $includeProperty = 'compiler.ameba.c.include=@"' + $responsePath.Replace('\', '/') + '"'
    $ErrorActionPreference = 'Continue'
    & arduino-cli --config-file $configFile compile --fqbn $fqbn --board-options $boardOptions --build-property $includeProperty --build-path $buildDir --verbose $projectDir 2>&1 |
        Tee-Object -FilePath $logFile
    $compileExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = 'Stop'
    foreach ($localeKey in $previousLocale.Keys) {
        [Environment]::SetEnvironmentVariable($localeKey, $previousLocale[$localeKey], 'Process')
    }
}
$globalManifestAfter = Get-GlobalManifestState
$modelsVerified = $false
if ($globalManifestBefore -ne $globalManifestAfter) {
    Write-Error 'Global model manifest changed unexpectedly.'
    $compileExitCode = 1
}
if ($compileExitCode -eq 0) {
    $localTools = Join-Path $dataDir 'packages\realtek\tools\ameba_pro2_tools\1.4.5'
    $packedModels = (Get-Content -Raw -LiteralPath (Join-Path $localTools 'amebapro2_fwfs_nn_models.json') | ConvertFrom-Json).FWFS.files
    $modelsVerified = (($packedModels -join ',') -eq 'yolov4_tiny')
    if (-not $modelsVerified) {
        Write-Warning ('Unexpected model manifest: ' + ($packedModels -join ','))
        $compileExitCode = 1
    }
}
[ordered]@{
    startedAt = $startedAt.ToString('o')
    finishedAt = (Get-Date).ToString('o')
    exitCode = $compileExitCode
    fqbn = $fqbn
    boardOptions = $boardOptions
    sketchSHA256 = (Get-FileHash -LiteralPath (Join-Path $projectDir 'IntrusionAlertPrototype.ino')).Hash
    modelsVerified = $modelsVerified
    globalManifestUnchanged = ($globalManifestBefore -eq $globalManifestAfter)
    log = $logFile
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logsDir 'last-build.json') -Encoding UTF8
exit $compileExitCode









