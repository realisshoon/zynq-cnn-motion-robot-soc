# 기존 진입점 호환: 출력은 Unity Tools/AgentReplay/build에만 생성합니다.
param(
    [string]$RepoRoot = "D:\working\final_project\zynq-cnn-motion-robot-soc",
    [string]$InputCsv = "$PSScriptRoot\example_pose2d_1280x720_20hz.csv",
    [int]$SettleMs = 1000,
    [string]$Python = "python"
)
& "$PSScriptRoot\build_and_convert_external_repo.ps1" -RepoRoot $RepoRoot -InputCsv $InputCsv -SettleMs $SettleMs -Python $Python
