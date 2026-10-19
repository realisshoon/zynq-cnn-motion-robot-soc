param(
    [string]$RepoRoot = "D:\working\final_project\zynq-cnn-motion-robot-soc",
    [string]$InputCsv = "$PSScriptRoot\example_pose2d_1280x720_20hz.csv",
    [int]$SettleMs = 1000,
    [string]$Python = "python"
)
$ErrorActionPreference = "Stop"
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    $Python = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
}
if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) { throw "Python 3 경로를 -Python으로 지정하세요." }
& $Python "$PSScriptRoot\build_canonical_replay.py" --repo $RepoRoot --input $InputCsv --settle-ms $SettleMs
if ($LASTEXITCODE -ne 0) { throw "canonical C bridge build/conversion 실패" }
