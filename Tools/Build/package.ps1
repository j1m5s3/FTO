# Packages FTO for Windows into Build/Package (zip the Windows folder to share).
#   powershell -ExecutionPolicy Bypass -File Tools/Build/package.ps1 [-Config Development|Shipping]
param([string]$Config = "Development")

$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
$UAT = "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat"
$UATArgs = @(
    "BuildCookRun",
    "-project=`"$Root\FTO.uproject`"",
    "-noP4", "-utf8output", "-unattended",
    "-platform=Win64", "-clientconfig=$Config",
    "-build", "-cook", "-stage", "-pak", "-iostore", "-archive",
    "-archivedirectory=`"$Root\Build\Package`""
)
& $UAT @UATArgs
exit $LASTEXITCODE
