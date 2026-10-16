param(
    [Parameter(Mandatory=$true)][string]$PlayerExe,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$SingleRoleCsv
)
$ErrorActionPreference='Stop'
$PlayerExe=(Resolve-Path -LiteralPath $PlayerExe).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a new validation directory; preserve existing results.'}
if($SingleRoleCsv){$SingleRoleCsv=(Resolve-Path -LiteralPath $SingleRoleCsv).Path}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$cases=@(
 @('CameraOverlay','--camera-overlay-verify','overlay_result.txt'),
 @('PortOwner','--csv-port-owner-verify','port_owner_result.txt'),
 @('G51','--g51-source-switch-verify','g51_source_result.txt'),
 @('QuickHUD','--records-quick-hud-verify','quick_hud_result.txt'),
 @('Records','--records-ko-ux-verify','records_ko_ux_result.txt'),
 @('CSV','--csv-playback-verify','csv_result.txt'),
 @('Studio','--studio-dock-verify','studio_ui_result.txt')
)
if($SingleRoleCsv){$cases+=,@('SingleRole','--csv-single-role-verify','single_role_result.txt')}
$results=@()
foreach($case in $cases){
    $testOut=Join-Path $OutputDirectory $case[0]
    New-Item -ItemType Directory -Path $testOut | Out-Null
    $testLog=Join-Path $OutputDirectory ($case[0]+'-player.log')
    $testArgs=@('-screen-fullscreen','0','-screen-width','1280','-screen-height','720',$case[1],('"'+$testOut+'"'),'-logFile',('"'+$testLog+'"'))
    if($case[0] -eq 'SingleRole'){$testArgs+=@('--csv-single-file',('"'+$SingleRoleCsv+'"'))}
    $owned=Start-Process -FilePath $PlayerExe -ArgumentList $testArgs -WindowStyle Hidden -PassThru
    Write-Output ('START '+$case[0]+' PID '+$owned.Id)
    $deadline=(Get-Date).AddSeconds(240)
    $resultFile=Join-Path $testOut $case[2]
    $report=''
    try {
        do {
            Start-Sleep -Milliseconds 500
            if(Test-Path -LiteralPath $resultFile){$report=Get-Content -LiteralPath $resultFile -Raw}
        } until($report -match 'RESULT=' -or (Get-Date) -gt $deadline -or $owned.HasExited)
        if($report -notmatch 'RESULT=PASS'){throw ('FAIL/timeout '+$case[0]+': '+$resultFile)}
        $results+=@{case=$case[0];result='PASS';checks=([regex]::Matches($report,'(?m)^PASS(?::| )')).Count}
        $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
        Write-Output ('PASS '+$case[0])
    } finally {
        $current=Get-Process -Id $owned.Id -ErrorAction SilentlyContinue
        if($current -and $current.Path -eq $PlayerExe){Stop-Process -Id $owned.Id}
    }
}
Write-Output 'All requested offline Player checks passed. No physical COM access is requested by these fixtures.'
