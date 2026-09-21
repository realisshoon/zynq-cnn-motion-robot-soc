<#
  robot_arm Vitis 작업환경 재현 스크립트 (Vitis 2020.2, Windows)
  (자세한 설명: robot_arm/README.md 의 'Vitis 작업환경' 절)

  하는 일 (새 워크스페이스에서 처음부터):
    1) XSA로 플랫폼(standalone, ps7_cortexa9_0)을 만들고 BSP를 빌드한다.
    2) 빈 앱 프로젝트를 만들고 저장소의 robot_arm/src 를 "링크"(복사 아님)로 붙인다.
    3) include 경로 2개, 컴파일 심볼, 링크 라이브러리(-lm)를 설정한다.
    4) 빌드에서 빼야 할 파일 2개(main.c, main_integration_shape.c)를 .cproject 에 등록한다.
    5) 앱을 빌드한다.

  주의
    - 워크스페이스 경로는 짧아야 한다(80자 이하, 예: D:\vws). Windows 경로 길이 제한(260자) 때문이다.
    - 이 스크립트를 돌리는 동안 Vitis IDE는 이 워크스페이스를 열지 않은 상태여야 한다.
    - xsct 임시폴더(.Xil)와 로그는 "<워크스페이스>_setup_logs" 폴더에 만들어져서 저장소를 더럽히지 않는다.
    - 링크 안의 파일을 Vitis에서 편집/삭제하면 저장소 원본이 바뀐다. 편집은 다른 편집기에서 한다.

  사용 예 (XSA는 기본으로 robot_arm\vitis\xsa\robot_test_wrapper.xsa 를 쓴다):
    powershell -ExecutionPolicy Bypass -File robot_arm\vitis\setup_vitis.ps1 -Workspace D:\vws
#>
param(
    [Parameter(Mandatory = $true)][string]$Workspace,
    [string]$Xsa,                        # 기본값: 이 스크립트 옆의 xsa\robot_test_wrapper.xsa
    [string]$RepoRoot,                   # 기본값: 이 스크립트의 상위 폴더(robot_arm)
    [string]$VitisBin = "C:\Xilinx\Vitis\2020.2\bin",
    [string]$PlatformName = "robot_test_wrapper",
    [string]$AppName = "robot_testbench"
)

$ErrorActionPreference = "Stop"

# Windows PowerShell 5.1 에서는 param 기본값 안의 $PSScriptRoot 가 비어 있어서 본문에서 계산한다.
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Xsa) { $Xsa = Join-Path $scriptDir "xsa\robot_test_wrapper.xsa" }
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path }
function ToTcl([string]$p) { return ($p -replace '\\', '/') }

# ---- 사전 점검 ----
$xsct = Join-Path $VitisBin "xsct.bat"
if (-not (Test-Path $xsct)) { throw "xsct.bat 을 찾을 수 없습니다: $xsct (Vitis 2020.2 경로를 -VitisBin 으로 지정)" }
if (-not (Test-Path $Xsa)) { throw "XSA 파일이 없습니다: $Xsa" }
foreach ($d in @("src", "include", "config")) {
    if (-not (Test-Path (Join-Path $RepoRoot $d))) { throw "저장소 폴더가 없습니다: $(Join-Path $RepoRoot $d)" }
}
if (Test-Path (Join-Path $Workspace ".metadata")) { throw "이미 워크스페이스가 있습니다: $Workspace (새 폴더를 지정하세요)" }

# Windows 경로 길이 제한(260자): 플랫폼(BSP) 안쪽 경로가 워크스페이스 아래로 약 170자까지 깊어진다.
# 그래서 워크스페이스 경로는 짧아야 한다(권장 예: D:\vws). 긴 경로에서는 BSP 생성이
# "파일 이름이나 확장명이 너무 깁니다"(boost::filesystem::create_directory)로 실패한다.
$maxWorkspaceLen = 80
$fullWs = [IO.Path]::GetFullPath($Workspace)
if ($fullWs.Length -gt $maxWorkspaceLen) {
    throw "워크스페이스 경로가 너무 깁니다($($fullWs.Length)자 > ${maxWorkspaceLen}자): $fullWs`n짧은 경로(예: D:\vws)를 지정하세요."
}

New-Item -ItemType Directory -Force $Workspace | Out-Null
$Workspace = (Resolve-Path $Workspace).Path
$Xsa = (Resolve-Path $Xsa).Path
$logs = "{0}_setup_logs" -f $Workspace                # xsct 실행 위치 겸 로그 폴더(저장소 밖)
New-Item -ItemType Directory -Force $logs | Out-Null

function Run-Xsct([string]$tclText, [string]$name) {
    $tcl = Join-Path $logs "$name.tcl"
    Set-Content -Path $tcl -Value $tclText -Encoding ascii
    $out = Join-Path $logs "$name.out"; $err = Join-Path $logs "$name.err"
    $p = Start-Process -FilePath $xsct -ArgumentList "`"$tcl`"" -WorkingDirectory $logs -NoNewWindow -PassThru `
         -RedirectStandardOutput $out -RedirectStandardError $err
    $null = $p.Handle    # Windows PowerShell 5.1: 핸들을 미리 잡아 두지 않으면 종료 후 ExitCode 가 비어 나온다
    if (-not $p.WaitForExit(900000)) { $p.Kill(); throw "xsct 시간 초과($name)" }
    $p.WaitForExit()     # 출력 리다이렉트가 끝날 때까지 대기
    $code = $p.ExitCode
    $errText = if (Test-Path $err) { Get-Content $err -Raw } else { "" }
    if (($null -ne $code -and $code -ne 0) -or $errText -match "(?m)^(error|ERROR)") { Write-Host $errText; throw "xsct 실패($name, 종료코드 $code). 로그: $logs" }
    Write-Host "  [OK] $name"
}

$ws = ToTcl $Workspace; $repo = ToTcl $RepoRoot; $xsaT = ToTcl $Xsa

# ---- 1단계: 플랫폼 + 앱 + 링크 + 설정 ----
Write-Host "1/3 플랫폼 생성(BSP 빌드 포함, 몇 분 걸림), 앱 생성, 링크, 설정"
$phase1 = @"
setws $ws
platform create -name $PlatformName -hw $xsaT -proc ps7_cortexa9_0 -os standalone
platform generate
app create -name $AppName -platform $PlatformName -domain standalone_domain -template {Empty Application}
importsources -name $AppName -path $repo/src -soft-link
app config -name $AppName -add include-path $repo/include
app config -name $AppName -add include-path $repo/config
app config -name $AppName -add define-compiler-symbols SERVO_PWM_DRIVER_USE_XILINX
app config -name $AppName -add libraries m
puts "include-path: [app config -name $AppName include-path]"
puts "symbols: [app config -name $AppName define-compiler-symbols]"
puts "libraries: [app config -name $AppName libraries]"
"@
Run-Xsct $phase1 "phase1_platform_app"

# ---- 2단계: 빌드에서 뺄 파일 등록 (xsct에 제외 명령이 없어 .cproject 를 직접 수정) ----
Write-Host "2/3 빌드 제외 파일 등록"
$cp = Join-Path (Join-Path $Workspace $AppName) ".cproject"
$txt = [IO.File]::ReadAllText($cp)
if (([regex]::Matches($txt, 'excluding="_ide"')).Count -ne 2) { throw ".cproject 형식이 예상과 다릅니다(excluding=""_ide"" 2개 필요)" }
$txt = $txt.Replace('excluding="_ide"', 'excluding="_ide|src/main.c|src/human_target_angle/main_integration_shape.c"')
[IO.File]::WriteAllText($cp, $txt, (New-Object Text.UTF8Encoding($false)))
Write-Host "  [OK] .cproject 제외 항목 추가"

# ---- 3단계: 앱 빌드 ----
Write-Host "3/3 앱 빌드"
Run-Xsct "setws $ws`napp build -name $AppName`n" "phase3_build"

$dbg = Join-Path (Join-Path $Workspace $AppName) "Debug"
$elf = Join-Path $dbg "$AppName.elf"
if (Test-Path $elf) {
    Write-Host ("완료: {0}  ({1:N0} bytes)" -f $elf, (Get-Item $elf).Length)
    Write-Host "다음: Vitis를 열고 워크스페이스를 $Workspace 로 지정하세요. 실행은 앱 우클릭 -> Debug As -> Launch on Hardware 입니다."
} else {
    Write-Host "ELF 가 만들어지지 않았습니다. 링크 에러를 확인하세요:"
    Write-Host "  cd $dbg ; make all   (Vitis 의 gnuwin/toolchain 이 PATH 에 있는 셸에서)"
    Write-Host "  (src/integration/platform_vitis.c 가 저장소에 없으면 platform_init 등 5개가 미정의로 나옵니다)"
    exit 1
}
