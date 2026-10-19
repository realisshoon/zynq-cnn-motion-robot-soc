param([Parameter(Mandatory=$true)][string]$RobotRoot, [string]$Gcc='gcc.exe')
$ErrorActionPreference='Stop'
$RobotRoot=(Resolve-Path -LiteralPath $RobotRoot).Path
$Gcc=(Get-Command $Gcc -ErrorAction Stop).Source
$env:PATH=(Split-Path $Gcc -Parent)+';'+$env:PATH
function Source-Key([string]$path) { 'robot_arm/'+$path.Substring($RobotRoot.Length+1).Replace('\','/') }
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$manifest=Get-Content "$PSScriptRoot/source-manifest.json" -Raw | ConvertFrom-Json
$sources=@('forearm_mapping','pose_mapping','pose_math','pose_hand') | ForEach-Object {Join-Path $RobotRoot "src/human_target_angle/$_.c"}
$inputs=@($sources)+@(Get-ChildItem "$RobotRoot/include","$RobotRoot/config","$RobotRoot/src/human_target_angle" -Recurse -Filter '*.h' -File | Select-Object -ExpandProperty FullName)
$hashes=@{};foreach($p in $inputs){$hashes[$p]=(Get-FileHash -LiteralPath $p).Hash}
foreach($p in $manifest.source_sha256.PSObject.Properties){if((Get-FileHash -LiteralPath (Join-Path (Split-Path $RobotRoot -Parent) $p.Name)).Hash -ne $p.Value){throw "STEP 2 source differs: $($p.Name)"}}
$dll="$projectRoot/Assets/Plugins/x86_64/control_studio_xyz.dll"
& $Gcc -std=c99 -O2 -Wall -Wextra -flto -fwhole-program -shared -static-libgcc '-Wl,--exclude-all-symbols' "-I$RobotRoot/include" "-I$RobotRoot/config" "-I$RobotRoot/src/human_target_angle" "$PSScriptRoot/control_studio_xyz_bridge.c" @sources -lm -o $dll
if($LASTEXITCODE -ne 0){throw 'XYZ native build failed'}
foreach($p in $hashes.Keys){if((Get-FileHash -LiteralPath $p).Hash -ne $hashes[$p]){throw "Source changed: $p"}}
$portableHashes=@{};foreach($sourcePath in $hashes.Keys){$portableHashes[(Source-Key $sourcePath)]=$hashes[$sourcePath]}
@{abi=1;source_head=$manifest.source_head;source_sha256=$portableHashes;compiled_sources=@($sources | ForEach-Object { Source-Key $_ });dll_sha256=(Get-FileHash $dll).Hash;wrapper_sha256=(Get-FileHash "$PSScriptRoot/control_studio_xyz_bridge.c").Hash;output_abi2_unchanged=((Get-FileHash "$projectRoot/Assets/Plugins/x86_64/control_studio_v2.dll").Hash -eq $manifest.dll_sha256)} | ConvertTo-Json -Depth 5 | Set-Content "$PSScriptRoot/xyz-source-manifest.json" -Encoding utf8
