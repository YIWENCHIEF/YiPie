// YiPie 设置页状态中心（M2 T3 + 收尾波次）。
// 单一真相源 = C++ 内存配置；本模块只做：拉取/合并/防抖回写/权威回读。
// 校验仲裁全部在 C++（ParseConfig+NormalizeConfig），前端默认表仅用于
// 渲染缺省值 —— 与 src/config.h 的字段默认值保持一致（改动需同步）。

import { deepMerge, accumulatePatch } from "./patch.mjs";

// —— 默认模板（镜像 src/config.h，config 缺字段时兜底渲染）——
export const DEFAULTS = {
  gesture: {
    triggerButton: "right",
    dragThreshold: 25.0,
    coreRadius: 50.0,
    outerEscape: true,
    outerEscapeDistance: 186.0,
    disableOnModifier: false,
    disableOnFullScreen: true,
    blacklistProcesses: [],
    autoStart: false,
    isolationMode: "blacklist",
    whitelistProcesses: [],
  },
  appearance: {
    shape: "classic",
    theme: "dark",
    wheelRadius: 138.0,
    innerRadius: 52.0,
    showLabels: true,
    opacity: "mid",
    language: "auto",
    subWheelWidth: 56.0,
    iconSize: 28.0,
    labelMode: "all",
  },
  profiles: [
    {
      processName: "Global",
      sectorCount: 8,
      actions: Array.from({ length: 8 }, () => ({
        type: "", name: "", target: "", args: "", iconKey: "", subActions: [],
      })),
    },
  ],
};

// —— IPC 封装 ——
let msgId = 0;
const pending = new Map();
// 原生对话框类命令要等用户操作，超时放宽到 10 分钟
const LONG_TIMEOUT_CMDS = new Set(["browseProgram", "exportConfig", "importConfig"]);
window.chrome.webview.addEventListener("message", (e) => {
  let m;
  try { m = typeof e.data === "string" ? JSON.parse(e.data) : e.data; }
  catch { return; }
  const p = pending.get(m.id);
  if (!p) return;
  pending.delete(m.id);
  clearTimeout(p.timer);
  if (m.ok) p.resolve(m.result ?? {});
  else p.reject(new Error(m.error || "未知错误"));
});

export function ipc(cmd, args = {}, timeoutMs) {
  const id = ++msgId;
  const ms = timeoutMs ?? (LONG_TIMEOUT_CMDS.has(cmd) ? 600000 : 8000);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error("请求超时: " + cmd));
    }, ms);
    pending.set(id, { resolve, reject, timer });
    window.chrome.webview.postMessage(JSON.stringify({ id, cmd, args }));
  });
}

// —— 全局状态 ——
export const state = { config: null, _ui: { currentProfileName: "Global" } };
const renderSubs = [];
// subscribe 返回退订函数（抽屉等临时视图关闭时必须退订，防止泄漏与脏渲染）
export function subscribe(fn) {
  renderSubs.push(fn);
  return () => {
    const i = renderSubs.indexOf(fn);
    if (i >= 0) renderSubs.splice(i, 1);
  };
}
function notifyRender() { for (const fn of renderSubs) { try { fn(); } catch (e) { console.error(e); } } }

export function currentProfile() {
  const list = state.config.profiles || [];
  return list.find((p) => p.processName === state._ui.currentProfileName) ||
         list.find((p) => p.processName === "Global") || list[0];
}

// —— 保存：改动即时生效 + 防抖 ——
// 防抖窗口内的多次补丁先累积（不同字段都保留），到期一次性合并到权威配置上发送。
let saveTimer = null;
let pendingPatch = null;

async function flushSave() {
  const patch = pendingPatch;
  pendingPatch = null;
  if (!patch) return;
  const merged = deepMerge(state.config, patch);
  try {
    const res = await ipc("applyConfig", { config: merged });
    if (res && res.appearance) {
      state.config = deepMerge(DEFAULTS, res);  // C++ 回读为权威（含钳制）
      applyTheme();
      notifyRender();
    }
  } catch (err) {
    toast("保存失败：" + err.message, true);
    saveRollbackEpoch++;  // 通知临时视图（抽屉）丢弃乐观草稿，回退到服务端值
    await resync();  // 回滚 UI 到 C++ 现值
  }
}
// 保存失败代数：抽屉用它判断是否应放弃 lastCommitted 乐观保护
export const saveState = { get rollbackEpoch() { return saveRollbackEpoch; } };
let saveRollbackEpoch = 0;

// save(partial)：partial = 对 config 的深补丁（如 {gesture:{dragThreshold:30}}）
// 串行化：flush 必须排队执行。否则两次 flush 的 applyConfig 同时在途、响应
// 乱序回写 —— 后到的陈旧 merged 既覆盖 state.config，又丢前一次未落盘的变更。
let saveChain = Promise.resolve();
export function save(partial, { immediate = false } = {}) {
  pendingPatch = accumulatePatch(pendingPatch, partial);
  clearTimeout(saveTimer);
  if (immediate) {
    saveChain = saveChain.then(flushSave);
    return saveChain;
  }
  saveTimer = setTimeout(() => { saveChain = saveChain.then(flushSave); }, 400);
}

async function resync() {
  try {
    const cfg = await ipc("getConfig");
    state.config = deepMerge(DEFAULTS, cfg);
    applyTheme();
    notifyRender();
  } catch (err) {
    toast("配置重拉失败：" + err.message, true);
  }
}

// —— toast ——
let toastTimer = null;
export function toast(text, isError = false) {
  const el = document.getElementById("toast");
  el.textContent = text;
  el.classList.toggle("error", !!isError);
  el.classList.remove("hidden");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.add("hidden"), isError ? 5000 : 2800);
}

// —— 主题联动（设置页自身配色跟随 appearance.theme）——
export function applyTheme() {
  const t = state.config?.appearance?.theme || "dark";
  let eff = t;
  if (t === "system")   // M3d：跟随系统深浅色，matchMedia 实时响应
    eff = matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
  document.documentElement.dataset.theme = eff;
}
try {
  matchMedia("(prefers-color-scheme: light)")
    .addEventListener("change", () => applyTheme());
} catch { /* 老引擎不支持 addEventListener 时静默降级 */ }

// —— 页面注册表：各页模块 initXxx(root)，切页首次访问时初始化 ——
const pageInits = {};
const pageReady = {};
export function onPage(name, initFn) { pageInits[name] = initFn; }
function ensurePage(name) {
  if (pageReady[name] || !pageInits[name]) return;
  const root = document.getElementById("page-" + name);
  if (root) { pageInits[name](root); pageReady[name] = true; }
}

function wireNav() {
  const crumb = document.getElementById("crumb");
  const titles = { wheel: "轮盘 · 外观", gesture: "手势", system: "系统" };
  document.querySelectorAll(".nav-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const page = btn.dataset.page;
      document.querySelectorAll(".nav-btn").forEach((b) => b.classList.toggle("active", b === btn));
      document.querySelectorAll(".page").forEach((s) =>
        s.classList.toggle("hidden", s.id !== "page-" + page));
      crumb.textContent = titles[page] || page;
      ensurePage(page);
    });
  });
}

// —— 启动 ——
export async function boot() {
  const cfg = await ipc("getConfig");
  state.config = deepMerge(DEFAULTS, cfg);
  applyTheme();
  wireNav();
  ensurePage("wheel");  // 默认页
  notifyRender();
}
