// 轮盘页装配（M2 T4）：预览 + 抽屉 + Profile 条 + 扇区滑块。
// 单独成模块以打破 app.js <-> 子模块的循环 import（app.js 不 import 任何页模块）。
import { state, save, currentProfile, onPage, subscribe } from "./app.js";
import { renderPreview, bindPreview, loadGlyphs, setPreviewRefreshHook } from "./wheel-preview.js";
import { openDrawer, closeDrawer } from "./editor-drawer.js";
import { initProfileBar, initSectorSlider, onWheelInvalidate } from "./profile-bar.js";
import { buildAppearance } from "./page-settings.js";

export function initWheel() {
  loadGlyphs().then(() => render());  // 字形表就绪后重绘带图标
  setPreviewRefreshHook(() => { if (svg._refresh) svg._refresh(); });  // exe 图标异步就绪刷新
  const svg = document.getElementById("preview");
  const drawerHost = document.getElementById("drawer-host");
  let picked = -1;

  // 预览显式定尺：边长 = min(容器宽, 容器高) - 余量。CSS 百分比/aspect-ratio
  // 在该 flex 布局里不可靠（实测塌缩），JS 测量是唯一确定性方案。
  function fitPreviewSize() {
    const host = svg.parentElement;              // #preview-host（CSS 撑满列）
    const col = host ? host.parentElement : null; // .preview-col（有确定高度）
    const box = host && host.clientHeight > 60 ? host : col;
    if (!box) return;
    const w = box.clientWidth, h = box.clientHeight;
    if (w < 60 || h < 60) return;                // 未布局：保持上次尺寸
    const side = Math.max(240, Math.min(w, h) - 8);
    svg.style.width = side + "px";
    svg.style.height = side + "px";
  }
  window.addEventListener("resize", () => { fitPreviewSize(); });

  function render() {
    fitPreviewSize();
    const p = currentProfile();
    if (!p) { picked = -1; closeDrawer(); return; }
    if (picked >= p.sectorCount) { picked = -1; closeDrawer(); }
    renderPreview(svg, p, state.config.appearance, picked, svg._getHover ? svg._getHover() : -1);
    const crumb = document.getElementById("crumb");
    if (crumb) crumb.textContent = "轮盘 · " + (state._ui.currentProfileName || "Global");
  }

  bindPreview(svg, {
    onPick(i) {
      picked = i;
      render();
      openDrawer(drawerHost, i);
    },
    onPickSub(j) {
      // 点子扇叶 = 打开主槽抽屉并直接展开该子动作编辑器
      if (picked >= 0) openDrawer(drawerHost, picked, j);
    },
    async onReorder(from, to) {
      const p = currentProfile();
      if (!p) return;
      const acts = p.actions.map((a) => structuredClone(a));
      // 交换语义：只交换 from/to 两个槽位（与拖拽预览一致，不再整盘移位）
      const tmp = acts[from]; acts[from] = acts[to]; acts[to] = tmp;
      picked = to;   // 跟随被拖扇区落到新位置
      await save({ profiles: state.config.profiles.map((pp) =>
        pp.processName === p.processName ? { ...pp, actions: acts } : pp) },
        { immediate: true });
    },
  });
  svg._refresh = render;  // hover 变化时由 wheel-preview 回调重绘
  onWheelInvalidate(render);
  subscribe(render);
  initProfileBar(document.getElementById("profile-host"));
  initSectorSlider(document.getElementById("sector-slider-host"));
  buildAppearance(document.getElementById("appearance-host"));
  render();
}

onPage("wheel", initWheel);
