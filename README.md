# YiPie

轻量 Windows 径向手势轮盘（radial menu / 划轮盘）。按住触发键（默认鼠标右键）
划动唤出环形菜单，松手即执行对应扇区动作：启动程序、打开 URL 或模拟一组热键。
纯 Win32 + Direct2D 实现，自带所见即所得设置页（WebView2），空闲私有内存约 3 MB。

## 快速开始

1. 在仓库 **Releases** 页下载最新发布包（`yipie.exe` + `ui/` 文件夹，两者必须同级），双击 `yipie.exe`（无需安装）。
2. 任务栏右下角出现托盘图标，气泡提示 "YiPie 已启动"。
3. **双击托盘图标（或右键 → 设置…）打开设置页**：在轮盘预览上直接点扇区即可绑定动作
   （启动程序 / 模拟热键，带录制框与常用预设），扇区数、主题、透明度等改完即时生效。
4. 首次运行会在 `%APPDATA%\YiPie\config.json` 生成默认配置：
   一个 **8 向全空槽的 Global 轮盘**（空槽松手 = 静默取消，右键短按仍还原为普通右键点击）。
5. 给扇区配上动作后即可使用：**按住鼠标右键 → 划动到某个扇区 → 松手触发**。
   划动距离不足（默认 < 25px）会当作普通右键点击放行，不影响原有右键菜单。

托盘右键菜单：**启用 YiPie**（暂停/恢复全局钩子）、**设置…**、**重新加载配置**、**退出**。

> 设置页依赖系统 WebView2 运行时（Win11/新版 Win10 通常自带；缺失时打开设置页会提示并引导下载，约 2MB）。

## 编辑配置

推荐用设置页（见上）。JSON 直改仍是受支持的高级路径（改完托盘"重新加载配置"生效）：

- 完整带注释示例、全部字段说明表、triggerButton / 热键键名 / 动作类型取值清单：
  见 **[docs/config-example.md](docs/config-example.md)**。

构建（开发）：VS 2022 BuildTools + CMake + Ninja；
首次先 `powershell -File scripts/fetch-webview2.ps1` 拉取 WebView2 SDK（vendored 进仓库，幂等）。
`powershell -File scripts/build.ps1` 后跑 `ctest --test-dir build`（单测在 `tests/`，doctest + node:test）。
发布：`powershell -File scripts/publish.ps1`，产物在 `dist/`（`yipie.exe` 约 513 KB + `ui/`，Release，/MT）。

## 已知限制

- **仅支持 ASCII 文本**：`launch` 的路径/参数与 `hotkey` 的键名文本按 ASCII 处理，中文路径的程序或含中文的启动参数可能乱码失败。
- **开机自启**已提供设置页开关（写 `HKCU\...\Run`）；管理员权限场景仍需手动以管理员运行。
- **DPI**：已按 PerMonitorV2 感知，轮盘几何按按下点所在显示器缩放；150%+ 实屏回归待补测。
- **界面语言**：当前仅提供中文界面，英文界面在计划中。
- 内存以**私有工作集**为准（空闲约 3.3 MB，设置页开着约 5.2 MB）；任务管理器默认的"内存（工作集）"列
  含系统 DLL 共享映射（约 17 MB），那是所有 GUI 进程共用的，不是 YiPie 独占占用。
- 设置页依赖系统 WebView2 运行时（Evergreen）；缺失时核心手势功能正常，仅设置页不可用并提示下载。

## 项目结构

```
yipie/
├── src/                     # C++ 源码 (Win32 + Direct2D)
│   ├── mouse_hook.cpp       # 低级鼠标钩子 (WH_MOUSE_LL)
│   ├── gesture_engine.cpp   # 手势轨迹判定状态机
│   ├── wheel_window.cpp     # D2D 轮盘渲染窗口
│   ├── action_executor.cpp  # 热键模拟 / 程序启动 / URL
│   ├── config.cpp           # %APPDATA%\YiPie\config.json 读写
│   ├── icon_extract.cpp     # 快捷方式与 exe 图标提取
│   ├── ipc_router.cpp       # 主进程 ↔ 设置页 (WebView2 host object)
│   ├── settings_window.cpp  # WebView2 设置窗口
│   └── tray.cpp             # 系统托盘
├── ui/                      # 设置页前端 (HTML/CSS/JS, 由 exe 内嵌加载)
├── tests/                   # doctest C++ 单测 + node:test UI 逻辑测试
├── scripts/                 # 构建 / 发布 / 冒烟验证 PowerShell 脚本
├── third_party/             # vendored: doctest, rapidjson, WebView2 SDK
├── docs/                    # 配置说明与验收清单
├── cmake/                   # CMake 辅助脚本
└── CMakeLists.txt           # 构建入口
```

## 工作原理

- **钩子层**：`WH_MOUSE_LL` 低级钩子截获触发键（默认右键）的按下/抬起。位移未超过阈值（默认 25px）即松手时，注入一对干净的合成点击，宿主应用看到的仍是普通右键，原生右键菜单不受影响。
- **判定层**：`gesture_engine` 以按下点为圆心，按扇区数（4/8/12）将 360° 均分，实时把光标极角映射为选中扇区；外甩超出轮盘半径进入取消态。
- **渲染层**：透明分层窗口 + Direct2D 软件渲染绘制轮盘环带、扇区高亮、标签与子轮盘，Show 期间 8ms 定时器驱动动画。
- **执行层**：松手后由 `action_executor` 分发——热键经 `SendInput` + `MapVirtualKey` 扫描码转换（修饰键保持 15ms 再敲主键），程序/URL 经 `ShellExecute` 启动；支持扇区内停留展开的二级子轮盘。
- **进程隔离**：黑名单/白名单模式按前台进程名放行或屏蔽手势；全屏窗口自动暂停钩子。

## 许可

本项目采用 [MIT License](LICENSE) 开源。
