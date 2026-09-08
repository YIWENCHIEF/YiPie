param([string]$CmdLine)
$bt = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" && $CmdLine"
exit $LASTEXITCODE
