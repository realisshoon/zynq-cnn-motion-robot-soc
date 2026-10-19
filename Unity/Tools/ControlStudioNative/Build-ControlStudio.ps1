param([Parameter(Mandatory=$true)][string]$RobotRoot, [string]$Gcc='gcc.exe')
$ErrorActionPreference='Stop'
$RobotRoot=(Resolve-Path -LiteralPath $RobotRoot).Path
$Gcc=(Get-Command $Gcc -ErrorAction Stop).Source
$env:PATH=(Split-Path $Gcc -Parent)+';'+$env:PATH
function Source-Key([string]$path) { 'robot_arm/'+$path.Substring($RobotRoot.Length+1).Replace('\','/') }
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$outputDir=Join-Path $projectRoot 'Assets/Plugins/x86_64'
$sources=@('forearm_calibration','forearm_calibration_config','forearm_motion_control','forearm_safety_check','motion') | ForEach-Object {Join-Path $RobotRoot "src/robot_calibration/$_.c"}
$sources+=Join-Path $RobotRoot 'src/integration/agent_pipeline.c'
$headers=Get-ChildItem (Join-Path $RobotRoot 'include'),(Join-Path $RobotRoot 'config') -Filter '*.h' -Recurse -File | Select-Object -ExpandProperty FullName
$hashes=@{};foreach($f in @($sources)+@($headers)){$hashes[$f]=(Get-FileHash -LiteralPath $f).Hash}
$revision=(& git -C (Split-Path $RobotRoot -Parent) rev-parse HEAD).Trim()
if($revision -ne '4acc02f3eda814bfbe07d7d121215e49cfe80e27'){throw 'Pinned source revision differs; audit before rebuilding'}
# LTO removes unreachable original Agent1/Agent3/HAL paths. No replacement policy or HAL stubs.
& $Gcc -std=c99 -O2 -Wall -Wextra -flto -fwhole-program -DROBOT_TRACE -shared -static-libgcc '-Wl,--exclude-all-symbols' "-I$RobotRoot/include" "-I$RobotRoot/config" "$PSScriptRoot/control_studio_bridge.c" @sources -lm -o "$outputDir/control_studio_v2.dll"
if($LASTEXITCODE -ne 0){throw 'Native build failed'}
foreach($f in $hashes.Keys){if((Get-FileHash -LiteralPath $f).Hash -ne $hashes[$f]){throw "Source changed: $f"}}
$portableHashes=@{};foreach($sourcePath in $hashes.Keys){$portableHashes[(Source-Key $sourcePath)]=$hashes[$sourcePath]}
@{abi=2;source_head=$revision;compiled_sources=@($sources | ForEach-Object { Source-Key $_ });header_snapshot_scope='all robot_arm/include and config headers (superset of transitive inputs)';source_sha256=$portableHashes;defines=@('ROBOT_TRACE');link='-flto -fwhole-program (unreachable Agent1/Agent3/HAL eliminated)';exports=@('studio_abi_version','studio_limits','studio_create','studio_destroy','studio_validate','studio_submit','studio_submit_human','studio_tick');bridge_sha256=(Get-FileHash "$PSScriptRoot/control_studio_bridge.c").Hash;dll_sha256=(Get-FileHash "$outputDir/control_studio_v2.dll").Hash} | ConvertTo-Json -Depth 5 | Set-Content "$PSScriptRoot/source-manifest.json" -Encoding utf8
