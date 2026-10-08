param(
    [string]$SourceRoot,
    [string]$VisualStudio,
    [string]$Case
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$testRoot = [IO.Path]::GetFullPath($PSScriptRoot)
if (!$SourceRoot) {
    $ancestor = $testRoot
    while ($ancestor -and !$SourceRoot) {
        foreach ($candidate in @($ancestor, (Join-Path $ancestor 'source'), (Join-Path $ancestor 'robot_arm'))) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'src/record_replay/motion_library.c')) {
                $SourceRoot = $candidate
                break
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
}
if (!$SourceRoot) { throw 'Firmware root not found. Pass -SourceRoot with src/, include/, and config/.' }
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (!$VisualStudio) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'MSVC not found. Pass -VisualStudio with the installation directory.' }
    $VisualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$devCommand = Join-Path $VisualStudio 'Common7/Tools/VsDevCmd.bat'
if (!(Test-Path -LiteralPath $devCommand)) { throw "VsDevCmd.bat not found: $devCommand" }
$runRoot = Join-Path $testRoot ('runs/' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$relativeSources = @(
    'human_target_angle/agent1_stage.c', 'human_target_angle/pose_hand.c',
    'human_target_angle/pose_joint.c', 'human_target_angle/pose_mapping.c',
    'human_target_angle/pose_math.c', 'human_target_angle/pose_reconstruction.c',
    'human_target_angle/pose_tracking.c', 'human_target_angle/agent1_forearm_stage.c',
    'human_target_angle/forearm_mapping.c', 'robot_calibration/forearm_calibration.c',
    'robot_calibration/forearm_calibration_config.c', 'robot_calibration/forearm_motion_control.c',
    'robot_calibration/forearm_safety_check.c', 'robot_calibration/motion.c',
    'output_controller/output_control.c', 'output_controller/servo_config.c',
    'output_controller/servo_control.c', 'output_controller/servo_hal.c',
    'drivers/servo_pwm_driver.c', 'integration/agent_pipeline.c',
    'integration/cnn_app_event.c', 'record_replay/motion_record_replay.c',
    'record_replay/motion_sd.c', 'record_replay/motion_library.c'
)
$firmwareSources = @($relativeSources | ForEach-Object { Join-Path $SourceRoot "src/$_" })
$testSources = @('mocks/fatfs_mock.c', 'mocks/platform_cnn_stereo.c', 'test_record_boundaries.c') |
    ForEach-Object { Join-Path $testRoot $_ }
$manifestPaths = @($firmwareSources) + @(Join-Path $SourceRoot 'src/integration/main_integration.c')
foreach ($directory in @('include', 'config', 'src/human_target_angle', 'src/integration', 'src/cnn_firmware')) {
    $manifestPaths += @(Get-ChildItem -LiteralPath (Join-Path $SourceRoot $directory) -Recurse -File -Filter '*.h' | ForEach-Object FullName)
}
$manifestPaths += @($testSources) + @(Join-Path $testRoot 'run.ps1') + @(Join-Path $testRoot 'harness.h')
$manifestPaths += @(Get-ChildItem -LiteralPath (Join-Path $testRoot 'mocks') -File -Filter '*.h' | ForEach-Object FullName)
function Get-Manifest {
    @($manifestPaths | Sort-Object -Unique | ForEach-Object {
        [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
    })
}
$before = Get-Manifest
$before | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'source_before.json') -Encoding utf8
$results = @()
foreach ($role in @('LEFT', 'RIGHT')) {
    $roleRoot = Join-Path $runRoot $role
    New-Item -ItemType Directory -Path $roleRoot -Force | Out-Null
    $mainPath = (Join-Path $SourceRoot 'src/integration/main_integration.c').Replace('\', '/')
    [IO.File]::WriteAllText((Join-Path $roleRoot 'firmware_under_test.h'), '#include "' + $mainPath + '"' + [Environment]::NewLine)
    $includes = @((Join-Path $testRoot 'mocks'), $testRoot, $roleRoot)
    $includes += @('include', 'config', 'src/human_target_angle', 'src/integration', 'src/cnn_firmware') |
        ForEach-Object { Join-Path $SourceRoot $_ }
    $response = @('/nologo', '/utf-8', '/O2', '/std:c11', '/W3', '/WX', '/D_CRT_SECURE_NO_WARNINGS',
        '/DROBOT_SPLIT_BOARD_CONTROL=1', '/DROBOT_DUAL_ARM_ENABLE=0', "/DROBOT_STEREO_$role=1")
    $response += @($includes | ForEach-Object { '/I"' + $_ + '"' })
    $response += @(@($firmwareSources) + @($testSources) | ForEach-Object { '"' + $_ + '"' })
    $response += '/Fe:"' + (Join-Path $roleRoot 'test_record_boundaries.exe') + '"'
    $response += '/Fo:"' + $roleRoot + '\\"'
    $responsePath = Join-Path $roleRoot 'compile.rsp'
    [IO.File]::WriteAllLines($responsePath, $response, [Text.UTF8Encoding]::new($false))
    $batchPath = Join-Path $roleRoot 'build.cmd'
    [IO.File]::WriteAllLines($batchPath, @('@echo off',
        ('call "' + $devCommand + '" -arch=x64 -host_arch=x64'), 'if errorlevel 1 exit /b 1',
        ('cd /d "' + $roleRoot + '"'), ('cl @"' + $responsePath + '"'), 'exit /b %errorlevel%'), [Text.UTF8Encoding]::new($false))
    & $env:ComSpec /d /c $batchPath 2>&1 | Tee-Object -FilePath (Join-Path $roleRoot 'build.log')
    $buildExit = $LASTEXITCODE
    $testExit = $null
    $failures = @()
    if ($buildExit -eq 0) {
        $executable = Join-Path $roleRoot 'test_record_boundaries.exe'
        $testOutput = if ($Case) { & $executable $Case 2>&1 | Tee-Object -FilePath (Join-Path $roleRoot 'test.log') }
            else { & $executable 2>&1 | Tee-Object -FilePath (Join-Path $roleRoot 'test.log') }
        $testExit = $LASTEXITCODE
        $testOutput | Where-Object { $_ -match '^(CASE |FAIL |RESULT )' } | Write-Output
        $failures = @(Get-Content -LiteralPath (Join-Path $roleRoot 'test.log') | Where-Object { $_ -match '^FAIL ' })
    }
    $results += [pscustomobject]@{ role = $role; build_exit = $buildExit; test_exit = $testExit; failures = $failures; directory = $roleRoot }
}
$after = Get-Manifest
$after | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'source_after.json') -Encoding utf8
$changes = @(Compare-Object ($before | ForEach-Object { $_.path + ':' + $_.sha256 }) ($after | ForEach-Object { $_.path + ':' + $_.sha256 }))
$summary = [pscustomobject]@{
    source_root = $SourceRoot; run_directory = $runRoot; source_stable = $changes.Count -eq 0;
    changed_sources = $changes; compiler = 'MSVC x64 /std:c11 /W3 /WX /O2';
    storage = 'in-memory FatFS'; hardware = 'native driver mock'; results = $results
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'summary.json') -Encoding utf8
[IO.File]::WriteAllText((Join-Path $testRoot 'latest_run.txt'), $runRoot + [Environment]::NewLine)
Write-Host "Artifacts: $runRoot"
if ($changes.Count) { Write-Host 'INVALID: inputs changed during the run; rerun after parallel edits finish.'; exit 2 }
if (@($results | Where-Object { $_.build_exit -ne 0 -or $_.test_exit -ne 0 }).Count) { exit 1 }
exit 0
