# YiPie 配置编写指南

> M2 起绝大多数配置推荐直接在**设置页**（双击托盘图标）里改，改完即时生效。
> 本文档面向 JSON 直改 / 批量部署 / 理解配置结构的高级用户。

配置文件位置：`%APPDATA%\YiPie\config.json`（UTF-8 无 BOM）。
首次运行 YiPie 会自动生成包含**全部字段默认值**的配置文件；手动编辑保存后，
右键托盘图标 → **重新加载配置** 即刻生效（无需重启）。重载时解析失败会保留旧配置并气泡提示；
启动时就解析失败的话，本次以内置默认配置运行。非法数值不会报错，加载时统一钳制到下表范围。

## 完整示例

以下示例基于程序真实生成的默认配置改写：Global 轮盘 8 向、中键触发，
0 号扇区（正东）启动计算器、3 号扇区（西南）发送 `ctrl+alt+t`，其余留空槽；
另配一个 chrome 浏览器的 4 向专属轮盘。

```json
{
  "gesture": {
    "triggerButton": "middle",
    "dragThreshold": 25.0,
    "coreRadius": 50.0,
    "outerEscape": true,
    "outerEscapeDistance": 0.0,
    "disableOnModifier": false,
    "disableOnFullScreen": true,
    "blacklistProcesses": ["somegame.exe"],
    "autoStart": false
  },
  "appearance": {
    "shape": "classic",
    "theme": "light",
    "wheelRadius": 150.0,
    "innerRadius": 40.0,
    "showLabels": true,
    "opacity": "mid"
  },
  "profiles": [
    {
      "processName": "Global",
      "sectorCount": 8,
      "actions": [
        { "type": "launch", "name": "计算器", "target": "calc.exe", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "hotkey", "name": "终端", "target": "ctrl+alt+t", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" }
      ]
    },
    {
      "processName": "chrome",
      "sectorCount": 4,
      "actions": [
        { "type": "hotkey", "name": "新标签页", "target": "ctrl+t", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" },
        { "type": "", "name": "", "target": "", "args": "", "iconKey": "" }
      ]
    }
  ]
}
```

扇区顺序：`actions[i]` 从**正东（3 点钟方向）**起、**顺时针**排列（8 向时依次为
东、东南、南、西南、西、西北、北、东北）。`type` 为空字符串的是空槽：
划到空槽松手 = 静默取消，不产生任何动作。

## 字段说明表

### gesture（手势与触发）

| 键 | 类型 | 默认 | 有效范围 / 说明 |
|---|---|---|---|
| `triggerButton` | string | `"right"` | 触发键，取值见下表。未知值按 `right` 处理 |
| `dragThreshold` | number | `25.0` | 起手后划动多少像素判定为手势（否则松手还原为普通点击）。钳制 `[10, 60]` |
| `coreRadius` | number | `50.0` | 轮盘中心死区半径，回到圈内无选中。钳制 `[15, 120]`。实际生效值 = `max(15, min(该值, 0.6×dragThreshold))`（默认参数下约 15px） |
| `outerEscape` | bool | `true` | 是否启用"外滑取消"（划出取消距离后松手 = 取消） |
| `outerEscapeDistance` | number | `186.0` | 取消距离（像素）。仅 `outerEscape: true` 时有意义；`<=0` 时自动取 `wheelRadius × 1.5`（固定值不随 wheelRadius 变化）；否则钳制 `[140, 320]` |
| `disableOnModifier` | bool | `false` | true 时按住 ctrl/alt/shift/win 期间不触发起手 |
| `disableOnFullScreen` | bool | `true` | true 时前台窗口处于全屏状态不触发（游戏/放映友好） |
| `blacklistProcesses` | string[] | `[]` | 进程隔离黑名单：前台进程 exe 基名在列则完全不触发。大小写不敏感、自动去首尾空白；非字符串元素被丢弃 |
| `autoStart` | bool | `false` | true = 写入注册表 `HKCU\...\Run` 开机自启（值名 `YiPie`）；false = 移除。启动与重载时同步，失败仅气泡提示 |

### appearance（外观）

| 键 | 类型 | 默认 | 有效范围 / 说明 |
|---|---|---|---|
| `shape` | string | `"classic"` | 目前仅实现 classic 一种形状，此字段为 M3 预留（disc 形态） |
| `theme` | string | `"dark"` | `"dark"` 或 `"light"`；其他字符串按 dark 渲染 |
| `wheelRadius` | number | `138.0` | 轮盘外径（像素，按 96 DPI 逻辑像素填写；运行时自动按按下点所在显示器的缩放比例放大）。钳制 `[80, 200]` |
| `innerRadius` | number | `52.0` | 轮盘内径（圆环 hole）。钳制 `[0, wheelRadius − 20]` |
| `showLabels` | bool | `true` | 是否在扇区绘制 `name` 文字（仅当扇区数 ≤ 12 且 name 非空） |
| `opacity` | string | `"mid"` | 轮盘透明度档位：`low`(0.55) / `mid`(0.85，基准) / `high`(1.0)；其他字符串归一化为 mid |

### profiles（轮盘方案，按前台进程匹配）

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `processName` | string | `"Global"` | 匹配目标进程 exe 基名，不含 `.exe` 也可以（扩展名会被忽略），大小写不敏感。`"Global"` 为兜底方案，配置中必须存在（缺失时程序自动补一个默认 Global） |
| `sectorCount` | int | `8` | 扇区数，钳制 `[3, 16]` |
| `actions` | object[] | 空槽 ×N | 长度自动补齐/截断到 `sectorCount`，元素结构见下 |

### actions[i]（单个扇区动作）

| 键 | 类型 | 说明 |
|---|---|---|
| `type` | string | `""`（空槽）\| `"launch"` \| `"hotkey"`。M1 没有其它动作类型 |
| `name` | string | 扇区标签文字（`showLabels` 开启时显示），不影响执行 |
| `target` | string | `launch`：可执行文件/文档路径或 URL（经 ShellExecute "open"，所以 `"http://..."`、`"shell:"`、注册表协议均可用）；`hotkey`：热键字符串（见下表）。注意 M1 仅支持 ASCII 文本（见 README 已知限制） |
| `args` | string | 仅 `launch` 使用：命令行参数，可空 |
| `iconKey` | string | M1 保留字段，渲染层不读取，随便留空 |

## 取值清单

### triggerButton

| 值 | 含义 |
|---|---|
| `"right"` | 鼠标右键（默认） |
| `"middle"` | 鼠标中键 |
| `"x1"` | 侧键 后退（XBUTTON1） |
| `"x2"` | 侧键 前进（XBUTTON2） |

### hotkey 动作的 target 写法

加号分隔，如 `"ctrl+alt+t"`、`"win+d"`、`"shift+f10"`。最多一个主键，
修饰键顺序随意（内部归一化为 ctrl → alt → shift → win）。键名大小写不敏感。

**修饰键**（含全部别名）：

| 键 | 别名 |
|---|---|
| Ctrl | `ctrl`、`control` |
| Alt | `alt`、`menu` |
| Shift | `shift` |
| Win | `win`、`windows`、`super`、`cmd` |

**主键**：

| 类别 | 可用名称 |
|---|---|
| 字母 / 数字 | `a`–`z`、`0`–`9` |
| 编辑区 | `tab`、`esc`/`escape`、`enter`/`return`、`space`、`backspace`/`bksp`、`delete`/`del`、`insert`/`ins` |
| 导航区 | `home`、`end`、`pageup`/`pgup`、`pagedown`/`pgdn`、`printscreen`/`prtsc` |
| 方向键 | `up`、`down`、`left`、`right` |
| 功能键 | `f1`–`f24` |

未收录的名称（如 `plus`、`capslock`）会导致该次动作静默失败（M1 执行在独立线程，不弹提示），不影响其它扇区。

### 动作 type

| 值 | 行为 |
|---|---|
| `"launch"` | `ShellExecuteW("open", target, args)` 启动程序/打开文档或 URL |
| `"hotkey"` | 按 target 描述的组合键用 SendInput 模拟一次按键 |
| `""` | 空槽，松手静默取消 |

## 常见改法

- **换触发键**：`"triggerButton": "x2"`（前进侧键），右键恢复普通用途。
- **缩小轮盘**：`"wheelRadius": 100.0`，同时把 `innerRadius` 降到 `≤ 80`。
- **让划动更灵敏**：调低 `dragThreshold`（最小 10）。
- **游戏免打扰**：保持 `disableOnFullScreen: true`，或把游戏 exe 基名加进 `blacklistProcesses`。
- **给某软件单独配轮盘**：复制一个 profile，`processName` 填该软件进程名（如 `chrome`），改 `sectorCount` 与 `actions` 即可。
