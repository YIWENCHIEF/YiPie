// 共享槽位编辑器（M3b T3）：类型段控件 + 名称 + launch 组 + hotkey 组
//（录制框/预设芯片/测试按钮）。主槽与子槽共用；调用方只提供读写回调。
// 热键键名映射必须与 src/hotkey_parser.cpp 一致（改动需双侧同步核对）。
import { ipc, toast } from "./app.js";
import { openIconPicker } from "./icon-picker.js";
import { iconForJs } from "./wheel-preview.js";

const MOD_KEYS = new Set(["Control", "Alt", "Shift", "Meta"]);
const SPECIAL = {
  Tab: "tab", Escape: "esc", Enter: "enter", " ": "space", Backspace: "backspace",
  Delete: "delete", Insert: "insert", Home: "home", End: "end",
  PageUp: "pageup", PageDown: "pagedown", PrintScreen: "printscreen",
  ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
};
const CODE_FALLBACK = {
  Space: "space", Tab: "tab", Escape: "esc", Enter: "enter", Backspace: "backspace",
  Delete: "delete", Insert: "insert", Home: "home", End: "end",
  PageUp: "pageup", PageDown: "pagedown", PrintScreen: "printscreen",
  ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
};
function keyToName(e) {
  if (SPECIAL[e.key]) return SPECIAL[e.key];
  const k = e.key;
  // 中文 IME 激活时 e.key 变 "Process"，回退物理键位 e.code（真实环境实测踩坑）
  if (k === "Process" || k === "Unidentified" || k === "") {
    if (CODE_FALLBACK[e.code]) return CODE_FALLBACK[e.code];
    let m = /^Key([A-Z])$/.exec(e.code); if (m) return m[1].toLowerCase();
    m = /^Digit([0-9])$/.exec(e.code); if (m) return m[1];
    m = /^F(\d{1,2})$/.exec(e.code); if (m && +m[1] >= 1 && +m[1] <= 24) return "f" + m[1];
    return null;
  }
  const m = /^F(\d{1,2})$/i.exec(e.key);
  if (m && +m[1] >= 1 && +m[1] <= 24) return "f" + m[1];
  if (/^[a-z]$/i.test(e.key)) return e.key.toLowerCase();
  if (/^[0-9]$/.test(e.key)) return e.key;
  return null;
}
function buildHotkey(mods, main) {
  // 固定输出序 ctrl,alt,shift,win（与 C++ modifierOrder 一致）+ 主键最后
  const out = [];
  if (mods.has("Control")) out.push("ctrl");
  if (mods.has("Alt")) out.push("alt");
  if (mods.has("Shift")) out.push("shift");
  if (mods.has("Meta")) out.push("win");
  if (main) out.push(main);
  return out.join("+");
}

// 预设：[显示名, 组合键, 自动填入的默认动作名]。组合键须能被 C++ parser 解析。
const PRESETS = [
  ["复制", "ctrl+c", "复制"], ["粘贴", "ctrl+v", "粘贴"],
  ["剪切", "ctrl+x", "剪切"], ["撤销", "ctrl+z", "撤销"],
  ["重做", "ctrl+y", "重做"], ["保存", "ctrl+s", "保存"],
  ["全选", "ctrl+a", "全选"], ["查找", "ctrl+f", "查找"],
  ["纯文本粘贴", "ctrl+shift+v", "纯文本粘贴"], ["新建", "ctrl+n", "新建"],
  ["打开", "ctrl+o", "打开"], ["关闭", "ctrl+w", "关闭"],
  ["切窗口", "alt+tab", "切换窗口"], ["常驻切换器", "ctrl+alt+tab", "常驻切换器"],
  ["任务视图", "win+tab", "任务视图"], ["显示桌面", "win+d", "显示桌面"],
  ["文件资源管理器", "win+e", "资源管理器"], ["锁屏", "win+l", "锁屏"],
  ["系统搜索", "win+s", "系统搜索"], ["截图", "win+shift+s", "截图"],
  ["设置", "win+i", "系统设置"], ["运行", "win+r", "运行"],
  ["输入法切换", "ctrl+shift", "切换输入法"],
  // —— 编辑/导航 ——
  ["另存为", "ctrl+shift+s", "另存为"], ["打印", "ctrl+p", "打印"],
  ["查找替换", "ctrl+h", "查找替换"], ["取消", "esc", "取消"],
  ["重置缩放", "ctrl+0", "重置缩放"],
  ["行首", "home", "行首"], ["行尾", "end", "行尾"],
  ["上翻页", "pageup", "上翻页"], ["下翻页", "pagedown", "下翻页"],
  ["删除", "delete", "删除"],
  // —— 窗口管理 ——
  ["关闭窗口", "alt+f4", "关闭窗口"], ["最小化", "win+down", "最小化"],
  ["最大化", "win+up", "最大化"], ["贴靠左半屏", "win+left", "贴靠左半屏"],
  ["贴靠右半屏", "win+right", "贴靠右半屏"], ["刷新", "f5", "刷新"],
  // —— 虚拟桌面 ——
  ["新建桌面", "win+ctrl+d", "新建虚拟桌面"],
  ["左移桌面", "win+ctrl+left", "桌面左移"],
  ["右移桌面", "win+ctrl+right", "桌面右移"],
  ["关闭桌面", "win+ctrl+f4", "关闭虚拟桌面"],
  // —— 系统功能 ——
  ["任务管理器", "ctrl+shift+esc", "任务管理器"], ["开始菜单", "win", "开始菜单"],
  ["复制路径", "ctrl+shift+c", "复制路径"], ["全屏", "f11", "全屏"],
  // —— 媒体与音量 ——
  ["音量+", "volumeup", "音量加"], ["音量-", "volumedown", "音量减"],
  ["静音", "volumemute", "静音"], ["播放/暂停", "playpause", "播放暂停"],
  ["下一曲", "nexttrack", "下一曲"], ["上一曲", "prevtrack", "上一曲"],
];

let uid = 0;
let chipsCollapsed = false;   // M3d：预设区折叠状态（跨抽屉实例共享）

// container 内注入编辑器 DOM。get() 返回当前槽 draft 快照；
// set(patch, immediate) 提交；showTest=false 隐藏测试按钮（子槽复用主槽测试即可）。
export function buildSlotFields(container, { get, set, showTest = true }) {
  const id = ++uid;
  const q = (s) => container.querySelector(s);
  const grpLaunchId = `grp-launch-${id}`, grpHotkeyId = `grp-hotkey-${id}`;
  container.insertAdjacentHTML("beforeend", `
    <div class="seg" id="seg-type-${id}">
      <button data-t="">空槽</button>
      <button data-t="launch">启动程序</button>
      <button data-t="hotkey">模拟热键</button>
    </div>
    <div class="field"><label>名称</label><input type="text" id="f-name-${id}" placeholder="扇区标签"></div>
    <div class="field"><label>图标</label>
      <span class="icon-preview" id="f-iconpv-${id}"></span>
      <button class="ghost" id="b-icon-${id}">选择图标…</button>
    </div>
    <div id="${grpLaunchId}" class="grp">
      <div class="field"><label>目标</label><input type="text" id="f-target-${id}" placeholder="exe/URL/文件夹路径"><button class="ghost" id="b-browse-${id}">浏览…</button></div>
      <div class="field"><label>参数</label><input type="text" id="f-args-${id}" placeholder="命令行参数（可空）"></div>
    </div>
    <div id="${grpHotkeyId}" class="grp">
      <div class="field"><label>组合键</label><div class="recorder" id="f-rec-${id}">点击后按下组合键</div></div>
      <div class="field"><label></label><input type="text" id="f-target-hk-${id}" placeholder="或直接输入，如 ctrl+alt+t"></div>
      <button class="chips-toggle" id="chips-toggle-${id}" type="button"></button>
      <div class="chips" id="preset-chips-${id}"></div>
    </div>
    ${showTest ? `<div style="margin-top:14px"><button class="primary" id="b-test-${id}">测试执行</button></div>` : ""}`);

  const segBtns = [...container.querySelectorAll(`#seg-type-${id} button`)];
  const grpLaunch = q(`#${grpLaunchId}`), grpHotkey = q(`#${grpHotkeyId}`);
  const iconPv = q(`#f-iconpv-${id}`), iconBtn = q(`#b-icon-${id}`);
  const nameInput = q(`#f-name-${id}`), targetInput = q(`#f-target-${id}`),
        argsInput = q(`#f-args-${id}`), targetHk = q(`#f-target-hk-${id}`),
        rec = q(`#f-rec-${id}`), testBtn = showTest ? q(`#b-test-${id}`) : null;

  function syncUI() {
    const a = get() || {};
    const t = a.type || "";
    segBtns.forEach((b) => b.classList.toggle("on", b.dataset.t === t));
    grpLaunch.style.display = t === "launch" ? "" : "none";
    grpHotkey.style.display = t === "hotkey" ? "" : "none";
    // 聚焦框保护：在途回读不得打断正在输入的框
    const ae = document.activeElement;
    const focused = [nameInput, targetInput, argsInput, targetHk].includes(ae) ? ae : null;
    const fv = focused ? focused.value : null;
    nameInput.value = a.name || "";
    renderIconPv(a);
    targetInput.value = a.target || "";
    targetHk.value = a.target || "";
    argsInput.value = a.args || "";
    if (focused && focused.isConnected) focused.value = fv;
    if (!armed) rec.textContent = t === "hotkey" && a.target ? a.target : "点击后按下组合键";
    if (testBtn) testBtn.disabled = !t || !a.target;
  }

  function renderIconPv(a) {
    iconPv.replaceChildren();
    const ref = iconForJs(a || {});
    if (ref.glyph) {
      const g = document.createElement("span");
      g.className = "glyph"; g.style.fontFamily = '"Segoe MDL2 Assets"';
      g.textContent = ref.glyph;
      iconPv.appendChild(g);
    } else if (ref.file) {
      const img = document.createElement("img");
      img.width = 20; img.height = 20;
      ipc("readIcon", { name: ref.file })
        .then((r) => { img.src = "data:image/png;base64," + r.base64; })
        .catch(() => { img.remove(); });
      iconPv.appendChild(img);
    } else {
      iconPv.textContent = "自动";
      iconPv.classList.add("muted");
      return;
    }
    iconPv.classList.remove("muted");
  }
  iconBtn.addEventListener("click", () => {
    openIconPicker(iconBtn, (get() || {}).iconKey || "", (key) => {
      set({ iconKey: key }, true);
      syncUI();
    });
  });

  segBtns.forEach((b) => b.addEventListener("click", () => {
    const t = b.dataset.t;
    // 切空槽连带清空参数，杜绝半配置槽位（normalize 也清洗，源头掐掉）
    set(t === "" ? { type: "", target: "", args: "", name: "" } : { type: t }, true);
    syncUI();
  }));
  nameInput.addEventListener("input", () => set({ name: nameInput.value }, false));
  targetInput.addEventListener("input", () => set({ target: targetInput.value }, false));
  argsInput.addEventListener("input", () => set({ args: argsInput.value }, false));
  targetHk.addEventListener("input", () => {
    rec.textContent = targetHk.value || "点击后按下组合键";
    set({ target: targetHk.value }, false);
  });

  // —— 录制框 ——
  let armed = false;
  const heldMods = new Set();
  const seenMods = new Set();
  function onRecKey(e) {
    if (!armed) return;
    e.preventDefault(); e.stopPropagation();
    if (e.key === "Escape") { disarm(); syncUI(); return; }
    if (MOD_KEYS.has(e.key)) { heldMods.add(e.key); seenMods.add(e.key); return; }
    if (e.key === "Backspace") { set({ target: "" }, true); disarm(); syncUI(); return; }
    const name = keyToName(e);
    if (name) { set({ target: buildHotkey(seenMods, name) }, true); disarm(); syncUI(); }
    // 未识别主键静默忽略；本机全局热键（如 ctrl+alt+t）会吞组合键属环境行为。
  }
  function onRecKeyUp(e) {
    if (!armed || !MOD_KEYS.has(e.key)) return;
    heldMods.delete(e.key);
    if (heldMods.size === 0) {
      if (seenMods.size > 0 && seenMods.size < 4)
        set({ target: buildHotkey(seenMods, null) }, true);
      disarm(); syncUI();
    }
  }
  function arm(on) {
    armed = on;
    rec.classList.toggle("armed", on);
    if (on) {
      heldMods.clear(); seenMods.clear();
      rec.textContent = "请按下组合键…（Esc 取消）";
      window.addEventListener("keydown", onRecKey, true);
      window.addEventListener("keyup", onRecKeyUp, true);
    } else {
      window.removeEventListener("keydown", onRecKey, true);
      window.removeEventListener("keyup", onRecKeyUp, true);
    }
  }
  function disarm() { arm(false); }
  rec.addEventListener("click", () => { if (armed) { disarm(); syncUI(); } else arm(true); });

  const chips = q(`#preset-chips-${id}`);
  const chipsToggle = q(`#chips-toggle-${id}`);
  function applyChipsCollapsed() {
    chips.style.display = chipsCollapsed ? "none" : "";
    chipsToggle.textContent = (chipsCollapsed ? "▸" : "▾") + " 常用预设";
  }
  chipsToggle.addEventListener("click", () => {
    chipsCollapsed = !chipsCollapsed;
    for (const t of document.querySelectorAll(".chips-toggle")) {
      const host = t.parentElement.querySelector(".chips");
      if (host) host.style.display = chipsCollapsed ? "none" : "";
      t.textContent = (chipsCollapsed ? "▸" : "▾") + " 常用预设";
    }
  });
  applyChipsCollapsed();
  for (const [label, hk, defName] of PRESETS) {
    const c = document.createElement("span");
    c.className = "chip";
    c.textContent = label;
    c.title = hk;
    c.addEventListener("click", () => {
      const cur = get() || {};
      const patch = { type: "hotkey", target: hk };
      // 自动填名称：仅当当前无名字，或名字仍是某个预设的默认名（未被用户改过）
      const isPresetName = cur.name === "" || PRESETS.some(([, , n]) => n === cur.name);
      if (isPresetName) patch.name = defName;
      set(patch, true);
      syncUI();
    });
    chips.appendChild(c);
  }

  q(`#b-browse-${id}`).addEventListener("click", async () => {
    try {
      const r = await ipc("browseProgram");
      if (r && r.path) { set({ target: r.path }, true); syncUI(); }
    } catch (err) { toast("浏览失败：" + err.message, true); }
  });
  if (testBtn) testBtn.addEventListener("click", async () => {
    const a = get();
    if (!a || !a.type || !a.target) { toast("先填好类型与目标再测试", true); return; }
    try {
      await ipc("testAction", { action: { ...a } });
      toast("已触发执行（launch 确认已启动；hotkey 仅确认已注入）");
    } catch (err) { toast("测试失败：" + err.message, true); }
  });

  syncUI();
  return {
    sync: syncUI,
    dispose: () => { disarm(); },
  };
}
