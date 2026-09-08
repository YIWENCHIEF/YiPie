param([string]$Config = "Debug")
$bt = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build build"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
