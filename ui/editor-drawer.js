// 动作编辑抽屉（M3b T3 重构；M3d 支持无缝切换编辑对象）。
// 字段编辑逻辑全在 slot-fields.js（主槽/子槽共用）；本模块负责：
//  - draft/lastCommitted 乐观更新机制（防"点 B 显示 A"的陈旧回写竞态）
//  - 子槽列表的增删与折叠 UI
//  - 保存链路（commit -> save(merged)）
//  - 抽屉已开时点别的扇区 = 就地切换编辑对象（不关再开，无闪烁）
import { state, save, toast, currentProfile, subscribe, saveState } from "./app.js";
import { buildSlotFields } from "./slot-fields.js";

const DIR_NAMES_8 = ["东", "东南", "南", "西南", "西", "西北", "北", "东北"];
const EMPTY_ACTION = () => ({ type: "", name: "", target: "", args: "", iconKey: "", subActions: [] });

let live = null;   // 当前打开抽屉的控制器（switchTo/close 用）

export function closeDrawer() {
  if (live) { live.dispose(); live = null; }
}

export function openDrawer(host, slotIndex, openSub = -1) {
  if (live) { live.switchTo(slotIndex, openSub); return; }  // 已开 -> 无缝切换
  const profileName = currentProfile().processName;
  let curSlot = slotIndex;

  const d = document.createElement("div");
  d.className = "drawer";
  d.innerHTML = `
    <button class="drawer-close" title="关闭">✕</button>
    <h3 id="drawer-title"></h3>
    <div class="sub">点击轮盘其它扇区可切换编辑对象</div>
    <div id="main-fields"></div>
    <div id="sub-section"></div>`;
  host.appendChild(d);
  requestAnimationFrame(() => d.classList.add("open"));

  const profile = () => state.config.profiles.find((p) => p.processName === profileName);
  const act = () => {
    const p = profile();
    if (!p) return null;
    return p.actions[curSlot] || null;
  };
  const dirOf = (i) => {
    const n = profile()?.sectorCount || 8;
    return n === 8 ? DIR_NAMES_8[i] : Math.round(i * (360 / n)) + "°";
  };

  let draft = { ...EMPTY_ACTION(), ...(act() || {}) };
  let lastCommitted = null;
  let seenRollback = saveState.rollbackEpoch;
  const fieldSets = [];

  function commit(partial, immediate = true) {
    draft = { ...draft, ...partial };
    lastCommitted = { ...draft };
    const p = profile();
    if (!p) return;
    const merged = { profiles: state.config.profiles.map((pp) =>
      pp.processName === profileName
        ? { ...pp, actions: pp.actions.map((x, i) => (i === curSlot ? { ...draft } : x)) }
        : pp) };
    save(merged, { immediate });
  }

  function syncAll() {
    const a = act();
    if (!a) return;
    if (saveState.rollbackEpoch !== seenRollback) {
      seenRollback = saveState.rollbackEpoch;
      lastCommitted = null;
    }
    if (lastCommitted && JSON.stringify(a) === JSON.stringify(lastCommitted)) {
      draft = { ...EMPTY_ACTION(), ...a };
      lastCommitted = null;
    }
    for (const f of fieldSets) f.sync();
    renderSubs();
  }

  const mainFields = buildSlotFields(d.querySelector("#main-fields"), {
    get: () => draft,
    set: (patch, immediate) => commit(patch, immediate),
  });
  fieldSets.push(mainFields);

  const subHost = d.querySelector("#sub-section");
  subHost.innerHTML = `
    <div class="sub-title">子动作（悬停展开二级轮盘，最多 4 个）</div>
    <div id="sub-rows"></div>`;
  const rowsHost = subHost.querySelector("#sub-rows");
  const expanded = new Set();
  const subFields = new Map();
  let lastStruct = "";

  function subs() {
    if (!Array.isArray(draft.subActions)) draft.subActions = [];
    return draft.subActions;
  }
  function commitSubs(immediate = true) {
    commit({ subActions: subs().map((s) => ({ ...EMPTY_ACTION(), ...s })) }, immediate);
  }
  function subSummary(s) {
    if (!s || !s.type) return "空子槽";
    const kind = s.type === "launch" ? "程序" : "热键";
    return `${s.name || "未命名"} · ${kind}${s.target ? " · " + s.target : ""}`;
  }
  function structKey() { return subs().length + "|" + [...expanded].sort().join(","); }

  function rebuildSubRows() {
    for (const [, f] of subFields) f.dispose?.();
    subFields.clear();
    rowsHost.replaceChildren();
    const list = subs();
    list.forEach((s, j) => rowsHost.appendChild(makeSubRow(j)));
    if (list.length < 4) {
      const add = document.createElement("button");
      add.className = "ghost";
      add.textContent = "+ 添加子动作";
      add.style.marginTop = "6px";
      add.addEventListener("click", () => {
        subs().push(EMPTY_ACTION());
        commitSubs();
        renderSubs();
      });
      rowsHost.appendChild(add);
    }
    lastStruct = structKey();
  }

  function makeSubRow(j) {
    const row = document.createElement("div");
    row.className = "sub-row";
    const head = document.createElement("div");
    head.className = "sub-head";
    const caret = document.createElement("span");
    caret.className = "sub-caret";
    caret.textContent = expanded.has(j) ? "▾" : "▸";
    const label = document.createElement("span");
    label.className = "sub-label";
    label.dataset.j = String(j);
    label.textContent = subSummary(subs()[j]);
    const rm = document.createElement("button");
    rm.className = "ghost sub-remove";
    rm.textContent = "移除";
    head.append(caret, label, rm);
    row.appendChild(head);

    if (expanded.has(j)) {
      const body = document.createElement("div");
      body.className = "sub-body";
      row.appendChild(body);
      subFields.set(j, buildSlotFields(body, {
        get: () => subs()[j] || null,
        set: (patch, immediate) => {
          const arr = subs();
          arr[j] = { ...EMPTY_ACTION(), ...arr[j], ...patch };
          commitSubs(immediate);
        },
        showTest: false,
      }));
    }
    const toggleRow = () => {
      if (expanded.has(j)) expanded.delete(j); else expanded.add(j);
      rebuildSubRows();
    };
    caret.addEventListener("click", toggleRow);
    head.addEventListener("click", (e) => { if (e.target !== rm) toggleRow(); });
    rm.addEventListener("click", (e) => {
      e.stopPropagation();
      subs().splice(j, 1);
      expanded.clear();
      commitSubs();
      rebuildSubRows();
    });
    return row;
  }

  function renderSubs() {
    if (structKey() !== lastStruct) { rebuildSubRows(); return; }
    for (const lab of rowsHost.querySelectorAll(".sub-label")) {
      const j = Number(lab.dataset.j);
      lab.textContent = subSummary(subs()[j]);
    }
    for (const [, f] of subFields) f.sync();
  }

  function setTitle() {
    d.querySelector("#drawer-title").textContent = `扇区 ${curSlot} · ${dirOf(curSlot)}`;
  }

  // 就地切换编辑对象：复用 DOM 与主槽编辑器实例，只重置 draft/子槽/标题。
  function switchTo(slotIndex, openSub) {
    curSlot = slotIndex;
    draft = { ...EMPTY_ACTION(), ...(act() || {}) };
    lastCommitted = null;
    expanded.clear();
    if (openSub >= 0 && openSub < subs().length) expanded.add(openSub);
    setTitle();
    rebuildSubRows();
    for (const f of fieldSets) f.sync();
  }

  function dispose() {
    d.classList.remove("open");
    for (const [, f] of subFields) f.dispose?.();
    subFields.clear();
    mainFields.dispose();
    unsub();
    const finish = () => d.remove();
    d.addEventListener("transitionend", finish, { once: true });
    setTimeout(finish, 260);   // transitionend 兜底
    live = null;
  }

  setTitle();
  switchTo(slotIndex, openSub);
  const unsub = subscribe(syncAll);
  d.querySelector(".drawer-close").addEventListener("click", closeDrawer);
  live = { switchTo, dispose };
}
