param(
    [string]$RepoRoot = "D:\working\final_project\zynq-cnn-motion-robot-soc",
    [string]$InputCsv = "$PSScriptRoot\video_dual_pose2d_1280x720_20hz.csv",
    [string]$OutputCsv = "$PSScriptRoot\dual_joint_trace.csv"
)

$ErrorActionPreference = "Stop"

$Robot = Join-Path $RepoRoot "robot_arm"
$Exe = Join-Path $PSScriptRoot "dual_pose_to_unity_trace.exe"
$Src = Join-Path $PSScriptRoot "dual_pose_to_unity_trace.c"

if (!(Test-Path $Robot)) {
    throw "robot_arm not found: $Robot"
}

if (!(Test-Path $InputCsv)) {
    throw "input CSV not found: $InputCsv"
}

$Sources = @(
    $Src,
    "$Robot\src\human_target_angle\pose_mapping.c",
    "$Robot\src\human_target_angle\forearm_mapping.c",
    "$Robot\src\human_target_angle\pose_tracking.c",
    "$Robot\src\human_target_angle\pose_reconstruction.c",
    "$Robot\src\human_target_angle\pose_joint.c",
    "$Robot\src\human_target_angle\pose_hand.c",
    "$Robot\src\human_target_angle\pose_math.c",
    "$Robot\src\robot_calibration\forearm_motion_control.c",
    "$Robot\src\robot_calibration\forearm_calibration.c",
    "$Robot\src\robot_calibration\forearm_calibration_config.c",
    "$Robot\src\robot_calibration\motion_limits.c",
    "$Robot\src\robot_calibration\motion_smoothing.c",
    "$Robot\src\robot_calibration\forearm_safety_check.c"
)

foreach ($s in $Sources) {
    if (!(Test-Path $s)) {
        throw "source missing: $s"
    }
}

$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (!$gcc) {
    throw "gcc not found in PATH. Use the same GCC environment as STEP 1."
}

# Keep GCC's own runtime DLLs ahead of unrelated libraries in the host PATH.
$env:PATH = (Split-Path -Parent $gcc.Source) + ";" + $env:PATH

$Args = @(
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-I$Robot\include",
    "-I$Robot\config",
    "-I$Robot\src\human_target_angle"
)

$Args += $Sources
$Args += @("-lm", "-o", $Exe)

Write-Host "READ-ONLY source root:"
Write-Host "  $RepoRoot"
Write-Host ""
Write-Host "Compiling dual preview..."

& $gcc.Source @Args

if ($LASTEXITCODE -ne 0) {
    throw "compile failed"
}

Write-Host "Compile PASS"
Write-Host "Running trace converter..."

& $Exe $InputCsv $OutputCsv

if ($LASTEXITCODE -ne 0) {
    throw "trace generation failed"
}

Write-Host ""
Write-Host "Generated:"
Write-Host "  $OutputCsv"
Write-Host ""
Write-Host "Git repository was READ ONLY."
Write-Host "All generated files are under:"
Write-Host "  $PSScriptRoot"
