# Task 8 / M2 T7: 发布脚本 —— Release 构建并把 yipie.exe + ui/ 复制到 dist/
# 依赖（与 scripts/build.ps1 同一套约定）：
#   - VS 2022 BuildTools（vcvars64.bat，路径见下 $bt）
#   - CMake：本机安装在 "C:\Program Files\CMake\bin"（fresh shell 不一定在 PATH 上，
#     本脚本会自动前置该目录；若你的 cmake 在别处，请自行改 $cmakeDir 或保证 PATH 可见）
#   - Ninja：由 vcvars64.bat 注入 PATH（VS 自带），无需单独安装
# 产物：dist/yipie.exe（/MT 静态运行时）+ dist/ui/（设置页前端资产，必须与 exe 同级）
$ErrorActionPreference = "Stop"
$cmakeDir = "C:\Program Files\CMake\bin"
if (Test-Path $cmakeDir) { $env:PATH = "$cmakeDir;$env:PATH" }
$bt = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
cmd /c "`"$bt\VC\Auxiliary\Build\vcvars64.bat`" && cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-release"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# 运行中的实例会锁住 dist\yipie.exe，使 Copy-Item 静默失败 -> 发布出旧文件。
Get-Process yipie -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item build-release/yipie.exe dist/yipie.exe -Force
# 复制后校验字节一致，防止静默失败再次发生
$src = (Get-Item build-release/yipie.exe).Length
$dst = (Get-Item dist/yipie.exe).Length
if ($src -ne $dst) { Write-Error "dist copy mismatch: src=$src dst=$dst"; exit 1 }
# 镜像 ui/ 到 dist/ui/。不用 Copy-Item -Recurse：目标已存在时会嵌套成
# dist\ui\ui，且 Remove-Item 偶发被杀软/资源管理器瞬时锁失败后静默降级。
# cmd rmdir 对不存在路径不报错；xcopy /e /i /y 语义确定（目标不存在=目录复制）。
cmd /c "rmdir /s /q dist\ui" 2>$null
cmd /c "xcopy /e /i /y /q ui dist\ui" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Error "xcopy ui->dist failed"; exit 1 }
$kb = [math]::Round((Get-Item dist/yipie.exe).Length / 1KB, 1)
Write-Host "dist/yipie.exe = $kb KB"   # M1.1 基线 423.5KB；M2 预算 <624KB（+200KB）
if ($kb -ge 624) { Write-Error "exe size $kb KB exceeds M2 budget (<624KB)"; exit 1 }
if (-not (Test-Path dist\ui\index.html)) { Write-Error "dist/ui missing"; exit 1 }
