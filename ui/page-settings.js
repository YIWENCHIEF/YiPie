// 外观 / 手势 / 系统页（M2 T5）。声明式绑定：控件 <-> config 路径。
// 校验仲裁在 C++（applyConfig 回读权威），这里只管渲染与提交。
import { state, save, ipc, toast, subscribe, applyTheme, onPage, DEFAULTS } from "./app.js";
import { getPath, patchOf } from "./patch.mjs";

const syncFns = [];
function collectSyncs(root) {
  const walk = (el) => { if (el._sync) syncFns.push(el._sync); for (const c of el.children) walk(c); };
  walk(root);
}
function syncAll() { for (const f of syncFns) { try { f(); } catch { /* 控件所属页被重建 */ } } }

// —— 控件工厂（均返回带 _sync() 的宿主元素）——
function row(label, control, note) {
  const d = document.createElement("div");
  d.className = "field";
  const l = document.createElement("label");
  l.textContent = label;
  d.append(l, control);
  if (note) {
    const n = document.createElement("span");
    n.className = "range-note";
    n.textContent = note;
    d.appendChild(n);
  }
  return d;
}

function makeSlider(spec) {
  const inp = document.createElement("input");
  inp.type = "range";
  inp.min = spec.min; inp.max = spec.max; inp.step = spec.step ?? 1;
  const val = document.createElement("span");
  val.style.cssText = "min-width:34px;text-align:right";
  const wrap = document.createElement("div");
  wrap.style.cssText = "flex:1;display:flex;align-items:center;gap:8px";
  wrap.append(inp, val);
  inp.addEventListener("input", () => { val.textContent = inp.value; });
  inp.addEventListener("change", () =>
    save(patchOf(spec.path, Number(inp.value)), { immediate: true }));
  wrap._sync = () => {
    const v = getPath(state.config, spec.path);
    if (spec.dynamicMax) inp.max = spec.dynamicMax();
    if (spec.disabledWhen) inp.disabled = spec.disabledWhen();
    inp.value = v; val.textContent = v;
  };
  return wrap;
}

function makeSelect(spec) {
  const sel = document.createElement("select");
  for (const [v, label] of spec.options) {
    const o = document.createElement("option");
    o.value = v; o.textContent = label;
    sel.appendChild(o);
  }
  sel.addEventListener("change", () =>
    save(patchOf(spec.path, sel.value), { immediate: true }));
  sel._sync = () => { sel.value = getPath(state.config, spec.path); };
  return sel;
}

function makeToggle(spec) {
  const wrap = document.createElement("label");
  wrap.className = "switch";
  const inp = document.createElement("input");
  inp.type = "checkbox";
  const knob = document.createElement("span");
  wrap.append(inp, knob);
  inp.addEventListener("change", () =>
    save(patchOf(spec.path, inp.checked), { immediate: true }));
  wrap._sync = () => { inp.checked = !!getPath(state.config, spec.path); };
  return wrap;
}

function cardEl(title) {
  const card = document.createElement("div");
  card.className = "card";
  const h = document.createElement("h2");
  h.textContent = title;
  card.appendChild(h);
  return card;
}

// —— 外观卡片（M3c 合并进轮盘页侧栏：调参即时预览）——
export function buildAppearance(root) {
  root.replaceChildren();
  const card = cardEl("外观");
  card.appendChild(row("形态",
    makeSelect({ path: "appearance.shape",
      options: [["classic", "经典扇区"], ["disc", "悬浮圆片"]] }),
    ""));
  card.appendChild(row("主题",
    makeSelect({ path: "appearance.theme",
      options: [["dark", "深色"], ["light", "浅色"], ["system", "跟随系统"]] })));
  card.appendChild(row("轮盘半径",
    makeSlider({ path: "appearance.wheelRadius", min: 80, max: 200, step: 2 }),
    "px"));
  card.appendChild(row("内径",
    makeSlider({
      path: "appearance.innerRadius", min: 0, max: 118, step: 2,
      dynamicMax: () => Math.max(0, state.config.appearance.wheelRadius - 20),
    })));
  card.appendChild(row("图标大小",
    makeSlider({ path: "appearance.iconSize", min: 16, max: 48, step: 1 }),
    "px"));
  card.appendChild(row("透明度",
    makeSelect({
      path: "appearance.opacity",
      options: [["low", "低（更通透）"], ["mid", "中（默认）"], ["high", "高（更实心）"]],
    })));
  card.appendChild(row("文字标签", makeToggle({ path: "appearance.showLabels" })));
  card.appendChild(row("标签显示",
    makeSelect({ path: "appearance.labelMode",
      options: [["all", "始终显示"], ["selected", "仅选中时"]] }),
    ""));
  root.appendChild(card);
  collectSyncs(card);
  syncAll();  // 懒构建页：建完立即从 state 拉一次权威值
}

// —— 手势页 ——
export function buildGesture(root) {
  root.replaceChildren();
  const card = cardEl("手势与触发");
  card.appendChild(row("触发键", makeSelect({
    path: "gesture.triggerButton",
    options: [["right", "鼠标右键"], ["middle", "中键"], ["x2", "侧键（后退）"], ["x1", "侧键（前进）"]],
  })));
  card.appendChild(row("拖拽阈值",
    makeSlider({ path: "gesture.dragThreshold", min: 10, max: 60 })));
  card.appendChild(row("中心死区",
    makeSlider({ path: "gesture.coreRadius", min: 15, max: 120 })));
  card.appendChild(row("外滑取消", makeToggle({ path: "gesture.outerEscape" })));
  card.appendChild(row("取消距离", makeSlider({
    path: "gesture.outerEscapeDistance", min: 140, max: 320,
    disabledWhen: () => !state.config.gesture.outerEscape,
  })));
  card.appendChild(row("修饰键穿透", makeToggle({ path: "gesture.disableOnModifier" })));
  card.appendChild(row("全屏时放行", makeToggle({ path: "gesture.disableOnFullScreen" })));
  const note = document.createElement("p");
  note.className = "hint";
  note.textContent = "实际生效值 = 配置值 × 屏幕缩放；死区上限为 0.6×拖拽阈值。";
  card.appendChild(note);

  // —— M3d 进程隔离卡：模式单选 + 黑/白名单两个 chips 编辑器 ——
  const isoCard = document.createElement("div");
  isoCard.className = "card";
  const ih = document.createElement("h3");
  ih.textContent = "进程隔离";
  ih.style.cssText = "margin:0 0 8px;font-size:14px";
  isoCard.appendChild(ih);

  // 模式单选
  const modeRow = document.createElement("div");
  modeRow.className = "field";
  const modeLabel = document.createElement("label");
  modeLabel.textContent = "模式";
  const modeSel = document.createElement("select");
  for (const [v, lab] of [["blacklist", "黑名单（列出的程序不触发）"],
                          ["whitelist", "白名单（仅列出的程序触发）"]]) {
    const o = document.createElement("option");
    o.value = v; o.textContent = lab;
    modeSel.appendChild(o);
  }
  modeSel.addEventListener("change", () =>
    save({ gesture: { isolationMode: modeSel.value } }, { immediate: true }));
  modeRow.append(modeLabel, modeSel);
  isoCard.appendChild(modeRow);
  syncFns.push(() => { modeSel.value = state.config.gesture.isolationMode || "blacklist"; });

  // 参数化 chips 编辑器：listPath = "blacklistProcesses" | "whitelistProcesses"
  function buildChipEditor(listPath, emptyHint) {
    const wrap = document.createElement("div");
    wrap.className = "iso-editor";
    const host = document.createElement("div");
    wrap.appendChild(host);
    const row = document.createElement("div");
    row.className = "field";
    const input = document.createElement("input");
    input.type = "text";
    input.placeholder = listPath === "whitelistProcesses" ? "如 notepad.exe" : "如 somegame.exe";
    input.style.flex = "1";
    const add = document.createElement("button");
    add.className = "ghost"; add.textContent = "添加";
    const fg = document.createElement("button");
    fg.className = "ghost"; fg.textContent = "加入当前前台";
    row.append(input, add, fg);
    wrap.appendChild(row);

    const getList = () => state.config.gesture[listPath] || [];
    function renderChips() {
      host.replaceChildren();
      const list = getList();
      if (!list.length) {
        const e = document.createElement("span");
        e.className = "hint";
        e.textContent = emptyHint;
        host.appendChild(e);
        return;
      }
      for (const name of list) {
        const chip = document.createElement("span");
        chip.className = "chip";
        chip.append(document.createTextNode(name));
        const x = document.createElement("i");
        x.className = "x"; x.textContent = "✕";
        x.addEventListener("click", async () => {
          await save({ gesture: { [listPath]: getList().filter((n) => n !== name) } },
                     { immediate: true });
          renderChips();
        });
        chip.appendChild(x);
        host.appendChild(chip);
      }
    }
    async function addName(raw) {
      const name = raw.trim().toLowerCase();
      if (!name) return;
      if (getList().includes(name)) { toast("已在列表中"); return; }
      await save({ gesture: { [listPath]: [...getList(), name] } }, { immediate: true });
      renderChips();
    }
    add.addEventListener("click", () => { addName(input.value); input.value = ""; });
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") { addName(input.value); input.value = ""; }
    });
    fg.addEventListener("click", async () => {
      try {
        const r = await ipc("foregroundProcess");
        if (r.process) addName(r.process);
        else toast("无法识别前台程序", true);
      } catch (err) { toast("获取前台程序失败：" + err.message, true); }
    });
    return { el: wrap, render: renderChips };
  }

  const blEditor = buildChipEditor("blacklistProcesses", "（空）划动手势在所有程序生效");
  const wlEditor = buildChipEditor("whitelistProcesses", "（空）白名单模式下所有程序都不触发，请添加");
  isoCard.appendChild(blEditor.el);
  isoCard.appendChild(wlEditor.el);

  // 按模式显示对应编辑器
  function applyModeVisibility() {
    const wl = (state.config.gesture.isolationMode || "blacklist") === "whitelist";
    blEditor.el.style.display = wl ? "none" : "";
    wlEditor.el.style.display = wl ? "" : "none";
  }
  syncFns.push(applyModeVisibility);
  function renderChips() { blEditor.render(); wlEditor.render(); applyModeVisibility(); }

  root.append(card, isoCard);
  collectSyncs(root);
  subscribe(renderChips);
  renderChips();
  syncAll();
}

// —— 系统页 ——
function buildSystem(root) {
  root.replaceChildren();
  const card = cardEl("系统");
  card.appendChild(row("开机自启", makeToggle({ path: "gesture.autoStart" }),
    "写入注册表 HKCU\\...\\Run"));

  const btnRow = document.createElement("div");
  btnRow.style.cssText = "display:flex;gap:10px;margin:14px 0";
  const bExport = document.createElement("button");
  bExport.className = "ghost"; bExport.textContent = "导出配置…";
  const bImport = document.createElement("button");
  bImport.className = "ghost"; bImport.textContent = "导入配置…";
  const bReset = document.createElement("button");
  bReset.className = "danger"; bReset.textContent = "恢复默认";
  btnRow.append(bExport, bImport, bReset);
  card.appendChild(btnRow);

  bExport.addEventListener("click", async () => {
    try {
      const r = await ipc("exportConfig");
      if (r.path) toast("已导出：" + r.path);
    } catch (err) { toast("导出失败：" + err.message, true); }
  });
  bImport.addEventListener("click", async () => {
    try {
      const r = await ipc("importConfig");
      if (r && !r.cancelled && r.profiles) {
        toast("配置已导入并生效");
        syncAll();
      }
    } catch (err) { toast("导入失败：" + err.message, true); }
  });
  bReset.addEventListener("click", async () => {
    if (!confirm("恢复全部默认配置？当前所有轮盘动作与设置将被清空。")) return;
    await save(structuredClone(DEFAULTS), { immediate: true });
    state._ui.currentProfileName = "Global";
    toast("已恢复默认配置");
  });

  const about = document.createElement("p");
  about.className = "hint";
  about.textContent = "YiPie M2 · 纯 Win32 + Direct2D + WebView2 · 下一步 M3：二级子轮盘、图标导入、disc 形态";
  card.appendChild(about);
  root.appendChild(card);
  collectSyncs(card);
  syncAll();
}

// 页面注册（app.js 的 onPage 表；切页首次访问时构建）
onPage("gesture", buildGesture);
onPage("system", buildSystem);
subscribe(syncAll);
