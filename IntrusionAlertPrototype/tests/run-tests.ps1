param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$storage = Join-Path (Split-Path -Parent $projectDir) '.amb82'
$testDir = Join-Path $storage 'intrusion-tests'
New-Item -ItemType Directory -Force $testDir | Out-Null
if (-not $Compiler) {
    $Compiler = Join-Path $storage 'host-tools\zig-windows-x86_64-0.13.0\zig.exe'
}
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Provide a local Zig compiler using -Compiler. No system installation is performed.' }
$oldCache = $env:ZIG_GLOBAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $testDir 'zig-cache'
    $binary = Join-Path $testDir 'logic_tests.exe'
    & $Compiler c++ -std=c++11 -Wall -Wextra -Werror (Join-Path $PSScriptRoot 'logic_tests.cpp') -o $binary
    if ($LASTEXITCODE -ne 0) { throw 'C++ test build failed' }
    & $binary | Tee-Object (Join-Path $testDir 'logic-results.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Logic test failed' }
    & node (Join-Path $PSScriptRoot 'web_tests.js') | Tee-Object (Join-Path $testDir 'web-results.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Web script test failed' }
} finally { $env:ZIG_GLOBAL_CACHE_DIR = $oldCache }
