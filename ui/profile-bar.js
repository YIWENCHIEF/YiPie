// Profile 条 + 扇区数滑块（M2 T4）。
// 下拉切当前编辑对象；「为当前前台程序新建专属」深拷贝 Global 动作；
// 删除仅对非 Global 开放（confirm 二次确认）。扇区数滑块 3~16，
// 数组补齐/截断语义与 C++ NormalizeConfig 一致（尾部截断、尾部补空槽）。
import { state, save, ipc, toast, currentProfile, subscribe } from "./app.js";

const EMPTY_ACTION = () => ({ type: "", name: "", target: "", args: "", iconKey: "" });

function resizeActions(actions, n) {
  const out = (actions || []).slice(0, n);
  while (out.length < n) out.push(EMPTY_ACTION());
  return out;
}

export function initProfileBar(host) {
  host.classList.add("card");
  host.innerHTML = `
    <div class="field">
      <label>编辑对象</label>
      <select id="prof-sel"></select>
    </div>
    <div style="display:flex;gap:8px;margin-top:8px">
      <button class="ghost" id="b-new">为当前前台程序新建专属</button>
      <button class="danger" id="b-del">删除当前</button>
    </div>
    <div class="hint" style="margin-top:6px">Global 为兜底轮盘，不可删除</div>`;
  const sel = host.querySelector("#prof-sel");
  const delBtn = host.querySelector("#b-del");

  function render() {
    const cur = state._ui.currentProfileName;
    sel.replaceChildren(...state.config.profiles.map((p) => {
      const o = document.createElement("option");
      o.value = p.processName;
      o.textContent = p.processName === "Global" ? "Global（全局默认）" : p.processName;
      return o;
    }));
    sel.value = cur;
    delBtn.style.display = cur === "Global" ? "none" : "";
  }
  sel.addEventListener("change", () => {
    state._ui.currentProfileName = sel.value;
    notifyWheel();
  });
  delBtn.addEventListener("click", async () => {
    const name = state._ui.currentProfileName;
    if (name === "Global") return;
    if (!confirm(`删除 ${name} 专属轮盘？该程序将回落使用 Global。`)) return;
    const merged = { profiles: state.config.profiles.filter((p) => p.processName !== name) };
    state._ui.currentProfileName = "Global";
    await save(merged, { immediate: true });
    toast(`已删除 ${name} 专属轮盘`);
    notifyWheel();
  });
  host.querySelector("#b-new").addEventListener("click", async () => {
    try {
      const r = await ipc("foregroundProcess");
      const proc = (r.process || "").replace(/\.exe$/i, "").toLowerCase();
      if (!proc) { toast("无法识别前台程序", true); return; }
      if (proc === "explorer") { toast("桌面/资源管理器建议直接编辑 Global", true); return; }
      if (state.config.profiles.some((p) => p.processName.toLowerCase() === proc)) {
        state._ui.currentProfileName = proc;
        toast(`已切换到 ${proc} 专属轮盘`);
        render(); notifyWheel();
        return;
      }
      const g = state.config.profiles.find((p) => p.processName === "Global");
      const clone = {
        processName: proc,
        sectorCount: g.sectorCount,
        actions: structuredClone(g.actions),
      };
      await save({ profiles: [...state.config.profiles, clone] }, { immediate: true });
      state._ui.currentProfileName = proc;
      toast(`已创建 ${proc} 专属轮盘（复制自 Global）`);
      render(); notifyWheel();
    } catch (err) { toast("新建失败：" + err.message, true); }
  });
  subscribe(render);
  render();
}

export function initSectorSlider(host) {
  host.classList.add("card");
  host.innerHTML = `
    <div class="field">
      <label>扇区数</label>
      <input type="range" id="sec-r" min="3" max="16" step="1">
      <span id="sec-v" style="min-width:24px;text-align:right"></span>
    </div>
    <div class="hint">3~16 任意整数；>12 时轮盘隐藏文字标签</div>`;
  const range = host.querySelector("#sec-r");
  const val = host.querySelector("#sec-v");

  function render() {
    const p = currentProfile();
    if (!p) return;
    range.value = p.sectorCount;
    val.textContent = p.sectorCount;
  }
  range.addEventListener("input", () => {
    val.textContent = range.value;
    // 预览即时重排（本地），不触发保存
    notifyWheel();
  });
  range.addEventListener("change", async () => {
    const p = currentProfile();
    const n = Number(range.value);
    await save({
      profiles: state.config.profiles.map((pp) =>
        pp.processName === p.processName
          ? { ...pp, sectorCount: n, actions: resizeActions(pp.actions, n) }
          : pp),
    }, { immediate: true });
    notifyWheel();
  });
  subscribe(render);
  render();
}

// 轮盘页模块间的小事件总线（避免循环 import）
let wheelListener = null;
export function onWheelInvalidate(fn) { wheelListener = fn; }
function notifyWheel() { if (wheelListener) wheelListener(); }
