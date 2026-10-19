param([int]$Camera = 0, [int]$Port = 5055)
$ErrorActionPreference = 'Stop'
$pythonPath = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) {
    throw 'MediaPipePoseTest/.venv가 없습니다. 기존 README 환경 준비 절차를 확인하세요.'
}
& $pythonPath -B (Join-Path $PSScriptRoot 'mediapipe_pose_test.py') --camera $Camera --udp --udp-host 127.0.0.1 --udp-port $Port --no-3d
if ($LASTEXITCODE -ne 0) { throw "MediaPipe sender 종료 코드: $LASTEXITCODE" }
