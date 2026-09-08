// 轮盘 SVG 预览（M2 T4；M3d 等比照片 + 拖拽排序）。
// 原则：预览 = 真实轮盘的等比例缩放。所有几何/图标/字号一律按配置的
// 真实逻辑像素绘制，最后整体乘一个统一组变换 fit —— 改任何单一参数的
// 相对比例都与实机一致。
// 拖拽排序（M3d）：每扇区内容绝对定位并包进 <g>，拖拽/让位用 SVG 属性
// transform="rotate(deg, C, C)"（自带旋转中心；CSS transform+origin 在
// SVG 组上不可靠，教训见 git 历史），rAF 指数平滑驱动让位/归位动画。
// 几何数学走 geometry.mjs（node 测试锁定）。
import { sectorAngles, subSectorAngles, annularSectorPath, labelAnchor, MAX_LABEL_SECTORS } from "./geometry.mjs";
import { ipc } from "./app.js";

// M3c: 字形表单一来源 ui/glyphs.json（boot 时 fetch 一次；未就绪时预览无图标，
// 不阻塞渲染）。iconForJs 与 src/icon_lookup.cpp IconFor 同优先级，改一侧核对另一侧。
let GLYPHS = null;
export async function loadGlyphs() {
  if (GLYPHS) return GLYPHS;
  try {
    const res = await fetch("glyphs.json");
    GLYPHS = await res.json();
  } catch { GLYPHS = {}; }
  return GLYPHS;
}
function glyphCh(id) {
  const e = (GLYPHS || {})[id];
  return e ? String.fromCodePoint(parseInt(e.cp, 16)) : null;
}
function matchPatterns(t) {
  for (const e of Object.values(GLYPHS || {}))
    if ((e.patterns || []).includes(t)) return String.fromCodePoint(parseInt(e.cp, 16));
  return null;
}
export function iconForJs(action) {
  const key = action.iconKey || "";
  if (key.startsWith("g:")) return { glyph: glyphCh(key.slice(2)) || glyphCh("app") };
  if (key.startsWith("f:")) return { file: key.slice(2) };
  if (!action.type) return {};
  const t = (action.target || "").toLowerCase();
  if (action.type === "launch") {
    // M3d：launch 优先位图（exe 自身图标，经 previewIcon IPC 异步取）；
    // glyph 存回退（基名匹配 -> app），与 C++ IconFor 语义一致。
    const base = t.split(/[\\/]/).pop() || "";
    const g = matchPatterns(base) || matchPatterns(base.replace(/\.[^.]+$/, ""));
    return { file: action.target, glyph: g || glyphCh("app") };
  }
  const g = matchPatterns(t);
  if (g) return { glyph: g };
  return { glyph: glyphCh("keyboard") };  // hotkey 未命中 -> keyboard
}

const exeIconCache = new Map();   // target -> dataURL | null(失败) | undefined(加载中)
let svgRefreshHook = null;
export function setPreviewRefreshHook(fn) { svgRefreshHook = fn; }
function ensureExeIcon(target) {
  if (exeIconCache.has(target)) return;
  exeIconCache.set(target, undefined);           // 占位防并发
  ipc("previewIcon", { target })
    .then((r) => { exeIconCache.set(target, "data:image/png;base64," + r.base64); })
    .catch(() => { exeIconCache.set(target, null); })
    .finally(() => { if (svgRefreshHook) svgRefreshHook(); });
}

const SIZE = 420;
const C = SIZE / 2;              // 圆心（viewBox 坐标系）
const NS = "http://www.w3.org/2000/svg";
// 缩放策略：内容外沿（含子环）自动放大填满画布（zoom-to-fit）。
// 仍是单一均匀变换 —— 预览与实机保持严格相似形，只是相机自动拉近。

function el(name, attrs = {}) {
  const n = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
  return n;
}

const FIT_MS = 180;   // 缩放过渡时长（easeOutCubic）
function easeOutCubic(k) { const u = 1 - k; return 1 - u * u * u; }

// 目标缩放：内容外沿（含子环）填满画布（留 12px 边距）
function targetFit(appearance, hasSub) {
  const contentR = appearance.wheelRadius + (hasSub ? (appearance.subWheelWidth ?? 56) : 0);
  return (C - 12) / Math.max(20, contentR);
}

export function renderPreview(svg, profile, appearance, pickedIndex, hoverIndex = -1) {
  const actions = profile.actions || [];
  const pickedAct0 = actions[pickedIndex];
  const hasSub = !!(pickedAct0 && pickedAct0.subActions && pickedAct0.subActions.length > 0);
  const target = targetFit(appearance, hasSub);

  let anim = svg._fitAnim;
  if (!anim) {
    anim = svg._fitAnim = { cur: target, from: target, target, t0: 0, raf: 0, args: null };
  }
  anim.args = { profile, appearance, pickedIndex, hoverIndex };

  if (anim.target !== target) {
    anim.from = anim.cur;              // 从当前视觉位置重新起缓
    anim.target = target;
    anim.t0 = performance.now();
    if (!anim.raf) {
      const step = (now) => {
        const a = svg._fitAnim;
        const k = Math.min(1, (now - a.t0) / FIT_MS);
        a.cur = a.from + (a.target - a.from) * easeOutCubic(k);
        const g = a.args;
        drawPreview(svg, g.profile, g.appearance, g.pickedIndex, g.hoverIndex, a.cur);
        if (k < 1 && a.cur !== a.target) a.raf = requestAnimationFrame(step);
        else { a.raf = 0; a.cur = a.target; }
      };
      anim.raf = requestAnimationFrame(step);
    }
  } else {
    drawPreview(svg, profile, appearance, pickedIndex, hoverIndex, anim.cur);
  }
}

// 全部按真实逻辑像素绘制（圆心 C），外层 <g> 统一 scale(fit) —— 等比照片。
function drawPreview(svg, profile, appearance, pickedIndex, hoverIndex, fit) {
  const dragging = !!svg._drag || !!svg._frozen;  // 拖拽/回正动画期间不重建
  const n = profile.sectorCount;
  const actions = profile.actions || [];
  const inner = Math.max(4, appearance.innerRadius);
  const outer = appearance.wheelRadius;
  const isDisc = appearance.shape === "disc";

  let root;
  if (dragging && svg._root) {
    root = svg._root;
    root.setAttribute("transform", `translate(${C} ${C}) scale(${fit}) translate(${-C} ${-C})`);
  } else {
    svg.replaceChildren();
    root = el("g", {
      transform: `translate(${C} ${C}) scale(${fit}) translate(${-C} ${-C})`,
    });
    svg.appendChild(root);
    svg._root = root;
  }

  // opacity 乘数（与渲染器 OpacityMul 同表）：只作用填充与文字
  const OPACITY_MUL = { low: 0.55, mid: 0.85, high: 1.0 };
  const om = OPACITY_MUL[appearance.opacity] ?? 0.85;
  const iconPx = appearance.iconSize || 28;
  // disc 圆钮半径（与渲染器同式：min(环带宽-2, 34)）
  const btnR = Math.max(6, Math.min((outer - inner) / 2 - 2, 34));

  if (!dragging) {
    root.replaceChildren();
    const groups = [];
    for (let i = 0; i < n; i++) {
      const { a0, a1, mid } = sectorAngles(i, n);
      const lit = i === pickedIndex || i === hoverIndex;
      const gEl = el("g", { class: "sector-group" });
      gEl.dataset.sector = String(i);
      root.appendChild(gEl);
      groups.push(gEl);
      const rMid = (inner + outer) / 2;
      if (isDisc) {
        gEl.appendChild(el("circle", {
          cx: C + rMid * Math.cos(mid), cy: C + rMid * Math.sin(mid),
          r: btnR * (i === pickedIndex ? 1.15 : 1),
          class: "sector disc" + (i === pickedIndex ? " selected" : "") + (i === hoverIndex ? " hl" : ""),
          opacity: lit ? "1" : String(om / 0.85),
        }));
      } else {
        gEl.appendChild(el("path", {
          d: annularSectorPath(C, C, inner, outer, a0, a1),
          class: "sector" + (i === pickedIndex ? " selected" : "") + (i === hoverIndex ? " hl" : ""),
          opacity: lit ? "1" : String(om / 0.85),
        }));
      }

      const act = actions[i];
      const icon = act ? iconForJs(act) : {};
      const hasGlyph = !!icon.glyph;
      const ax = C + rMid * Math.cos(mid);
      const ay = C + rMid * Math.sin(mid);
      // 与渲染器同式：半弦长 = iconR*sin(π/(2n)) - 4（内缩留隙），字形盒半高
      // = iconPx*0.75，fit = min(1, halfChord/盒半高)。保证预览与实机比例一致。
      const halfChord = Math.max(6, rMid * Math.sin(Math.PI / (2 * n)) - 4);
      const glyphHalf = iconPx * 0.75;
      const fit = Math.min(1, halfChord / glyphHalf);
      const dataUrl = icon.file ? exeIconCache.get(icon.file) : undefined;
      if (icon.file && dataUrl) {
        const half = Math.min(iconPx * 0.5, halfChord);
        gEl.appendChild(el("image", {
          href: dataUrl, x: ax - half, y: ay - half, width: half * 2, height: half * 2,
          class: "iconimg", opacity: lit ? "1" : String(om / 0.85),
        }));
      } else if (icon.file && dataUrl === undefined) {
        ensureExeIcon(icon.file);                 // 加载中：先画回退字形
        if (icon.glyph) {
          const gt = el("text", {
            x: ax, y: ay, "text-anchor": "middle", "dominant-baseline": "central",
            class: "glyph" + (lit ? " selected" : ""),
          });
          gt.style.fontSize = iconPx + "px";
          if (fit < 1) gt.setAttribute("transform", `translate(${ax} ${ay}) scale(${fit.toFixed(3)}) translate(${-ax} ${-ay})`);
          gt.textContent = icon.glyph;
          gEl.appendChild(gt);
        }
      } else if (hasGlyph) {
        const gt = el("text", {
          x: ax, y: ay, "text-anchor": "middle", "dominant-baseline": "central",
          class: "glyph" + (lit ? " selected" : ""),
        });
        gt.style.fontSize = iconPx + "px";
        if (fit < 1) gt.setAttribute("transform", `translate(${ax} ${ay}) scale(${fit.toFixed(3)}) translate(${-ax} ${-ay})`);
        gt.textContent = icon.glyph;
        gEl.appendChild(gt);
      }
      const hasIconVisual = (icon.file && dataUrl) || hasGlyph;
      const showLabel = appearance.showLabels && n <= MAX_LABEL_SECTORS;
      // M3d：labelMode=selected 时仅选中/悬停扇区显示文字标签（与渲染器一致）
      const selOnly = appearance.labelMode === "selected";
      if (showLabel && act && act.name && (!selOnly || i === pickedIndex || i === hoverIndex)) {
        let tx, ty;
        if (isDisc) { tx = ax; ty = ay + btnR + 13; }
        else {
          const lr = hasIconVisual ? inner + (outer - inner) * 0.82 : rMid;
          tx = C + lr * Math.cos(mid); ty = C + lr * Math.sin(mid);
        }
        const t = el("text", {
          x: tx, y: ty + 4, "text-anchor": "middle",
          class: "label" + (i === pickedIndex ? " selected" : ""),
        });
        t.textContent = act.name;
        gEl.appendChild(t);
      } else if (showLabel && !hasIconVisual) {
        const t = el("text", {
          x: ax, y: ay + 5, "text-anchor": "middle",
          class: "label empty" + (i === pickedIndex ? " selected" : ""),
        });
        t.textContent = "＋";
        gEl.appendChild(t);
      }
    }
    svg._groups = groups;
    svg._angles = groups.map(() => 0);      // 当前视觉旋转角（deg）
    svg._tw = null;                         // 固定时长缓动状态（提交重建后归零）

    // 子环示意（选中扇区有子动作时；子扇叶点击直达子动作编辑）
    const pickedAct = actions[pickedIndex];
    let subRing = null;
    if (pickedAct && pickedAct.subActions && pickedAct.subActions.length > 0) {
      subRing = el("g", { class: "subring" });
      root.appendChild(subRing);
      const sInner = outer;
      const sOuter = outer + (appearance.subWheelWidth ?? 56);
      const m = Math.min(4, pickedAct.subActions.length);
      // disc 形态：子环画圆钮（与渲染器同式：min(子环带宽/2-2, 24)）
      const subBtnR = Math.max(5, Math.min((sOuter - sInner) / 2 - 2, 24));
      subSectorAngles(pickedIndex, n, m).forEach((sa, j) => {
        let hit;
        if (isDisc) {
          const srMid = (sInner + sOuter) / 2;
          hit = el("circle", {
            cx: C + srMid * Math.cos(sa.mid), cy: C + srMid * Math.sin(sa.mid),
            r: subBtnR, class: "sector disc sub",
          });
        } else {
          hit = el("path", {
            d: annularSectorPath(C, C, sInner, sOuter, sa.a0, sa.a1),
            class: "sector sub",
          });
        }
        hit.dataset.sub = String(j);
        subRing.appendChild(hit);
        const sub = pickedAct.subActions[j];
        const sicon = sub ? iconForJs(sub) : {};
        const [x, y] = labelAnchor(C, C, sInner, sOuter, sa.mid);
        const sData = sicon.file ? exeIconCache.get(sicon.file) : undefined;
        if (sicon.file && sData) {
          const half = Math.min(iconPx, (appearance.subWheelWidth ?? 56) * 0.6) * 0.5;
          subRing.appendChild(el("image", { href: sData, x: x - half, y: y - half,
            width: half * 2, height: half * 2, class: "iconimg" }));
        } else {
        if (sicon.file && sData === undefined) ensureExeIcon(sicon.file);
        if (sicon.glyph) {
          const subFs = Math.min(iconPx, (appearance.subWheelWidth ?? 56) * 0.6);
          const gt = el("text", { x, y: y + subFs * 0.36, "text-anchor": "middle", class: "glyph" });
          gt.style.fontSize = subFs + "px";
          gt.textContent = sicon.glyph;
          subRing.appendChild(gt);
        } else if (appearance.showLabels && n <= MAX_LABEL_SECTORS) {
          const t = el("text", { x, y: y + 4, "text-anchor": "middle", class: "label" });
          t.textContent = sub && sub.name ? sub.name : "＋";
          subRing.appendChild(t);
        }
        }
      });
    }
    svg._subRing = subRing;
  }
  svg._geom = { n };
}

// 固定时长缓动引擎：dur=0 直接设值（拖拽跟手），dur>0 easeOut 插值
//（让位/归位）。提交排序时冻结让位姿态，save 回读重建后各扇区 base 角
// 恰好等于让位后的视觉角 —— 重建即无缝，无需回零动画（图标不跳变）。
function applyAngles(svg) {
  for (let i = 0; i < svg._angles.length; i++) {
    if (!svg._groups[i]) continue;
    const a = svg._angles[i];
    svg._groups[i].setAttribute("transform",
      Math.abs(a) > 0.01 ? `rotate(${a.toFixed(2)} ${C} ${C})` : "");
  }
}
function setTargets(svg, updates) {
  const now = performance.now();
  if (!svg._tw) svg._tw = svg._angles.map(() => null);
  for (const u of updates) {
    if (u.i < 0 || u.i >= svg._angles.length) continue;
    if (u.dur <= 0) { svg._angles[u.i] = u.angle; svg._tw[u.i] = null; }
    else svg._tw[u.i] = { from: svg._angles[u.i], to: u.angle, t0: now, dur: u.dur };
  }
  applyAngles(svg);
  if (!svg._tweenRaf) startTween(svg);
}
function startTween(svg) {
  const tick = (now) => {
    if (!svg._angles) { svg._tweenRaf = 0; return; }
    let active = false;
    for (let i = 0; i < svg._angles.length; i++) {
      const t = svg._tw && svg._tw[i];
      if (!t) continue;
      const k = Math.min(1, (now - t.t0) / t.dur);
      svg._angles[i] = t.from + (t.to - t.from) * easeOutCubic(k);
      if (k >= 1) { svg._tw[i] = null; svg._angles[i] = t.to; }
      else active = true;
    }
    applyAngles(svg);
    if (active) svg._tweenRaf = requestAnimationFrame(tick);
    else svg._tweenRaf = 0;
  };
  svg._tweenRaf = requestAnimationFrame(tick);
}

// 交互：点击选中 / 子扇叶直达编辑 / 悬停高亮 / 按住扇区拖拽排序（M3d）。
// 注意：不使用 setPointerCapture —— 它会把 click 目标重定向到 svg 根，
// 导致扇区点不进去（历史 bug）。拖拽监听挂在 window 上，点击选中在
// pointerup 里直接派发。
export function bindPreview(svg, { onPick, onPickSub, onReorder }) {
  let hover = -1;

  const centerPolar = (e) => {
    const r = svg.getBoundingClientRect();
    const dx = e.clientX - (r.left + r.width / 2);
    const dy = e.clientY - (r.top + r.height / 2);
    return { deg: (Math.atan2(dy, dx) * 180) / Math.PI, rad: Math.hypot(dx, dy),
             half: Math.min(r.width, r.height) / 2 };
  };
  const norm = (deg) => ((deg % 360) + 360) % 360;
  const wrap = (deg) => ((deg % 360) + 540) % 360 - 180;   // 归到 (-180,180]

  svg.addEventListener("click", (e) => {
    if (svg._clickSuppressed) { svg._clickSuppressed = false; return; }
    const sp = e.target.closest("[data-sub]");
    if (sp && onPickSub) onPickSub(Number(sp.dataset.sub));
  });
  svg.addEventListener("mousemove", (e) => {
    if (svg._drag) return;
    const p = e.target.closest("[data-sector]");
    const idx = p ? Number(p.dataset.sector) : -1;
    if (idx !== hover) { hover = idx; if (svg._refresh) svg._refresh(); }
  });
  svg.addEventListener("mouseleave", () => {
    if (!svg._drag) { hover = -1; if (svg._refresh) svg._refresh(); }
  });

  const onMove = (e) => {
    const d = svg._drag;
    if (!d) return;
    const { n } = svg._geom;
    const stepDeg = 360 / n;
    const { deg, rad } = centerPolar(e);
    // 增量累加：跨 ±180° 边界连续，不再 360° 跳变（历史 bug 根因之一）
    d.delta += wrap(deg - d.lastAngle);
    d.lastAngle = deg;
    if (!d.moved && Math.abs(d.delta) < 8) return;   // 阈值内视为点击
    d.moved = true;
    if (svg._subRing) svg._subRing.style.display = "none";
    // 交换语义：只有"被拖扇区"和"指针当前所在目标扇区"两个会动，其余不动
    //（问题1修复：旧版让位扫过区间全部挪一格 + 提交用插入 splice，导致整盘转）。
    if (rad > d.half * 0.35) {
      d.slots = Math.round(d.delta / stepDeg);
      let t = (((d.dg + d.slots) % n) + n) % n;
      if (t === d.dg) t = null;                    // 回到自己槽位 = 无交换对象
      if (t !== d.partner) {
        if (d.partner !== null)
          setTargets(svg, [{ i: d.partner, angle: 0, dur: 220 }]);  // 旧伙伴回位
        d.partner = t;
      }
    }
    const updates = [{ i: d.dg, angle: d.delta, dur: 0 }];   // 被拖组直接跟手
    if (d.partner !== null)
      updates.push({ i: d.partner, angle: wrap((d.dg - d.partner) * stepDeg), dur: 220 });
    setTargets(svg, updates);
  };

  const onUp = () => {
    const d = svg._drag;
    if (!d) return;
    svg._drag = null;
    window.removeEventListener("pointermove", onMove, true);
    window.removeEventListener("pointerup", onUp, true);
    window.removeEventListener("pointercancel", onUp, true);
    svg._clickSuppressed = true;
    setTimeout(() => { svg._clickSuppressed = false; }, 0);
    if (!d.moved) { if (onPick) onPick(d.dg); return; }      // 点击选中
    if (d.partner !== null && onReorder) {
      // 双方各自滑到对方槽位（约 200ms），动画结束再提交交换 —— 重建时
      // base 角恰好等于视觉角，无跳变。
      const stepDeg = 360 / svg._geom.n;
      // 最短弧：相邻槽（如 7↔0 跨 0°）各转 stepDeg，而非绕 315° 大圈
      setTargets(svg, [
        { i: d.dg, angle: wrap((d.partner - d.dg) * stepDeg), dur: 200 },
        { i: d.partner, angle: wrap((d.dg - d.partner) * stepDeg), dur: 200 },
      ]);
      const other = d.partner;
      const commit = () => { svg._frozen = false; onReorder(d.dg, other); };
      svg._frozen = true;
      setTimeout(commit, 210);
    } else {
      setTargets(svg, svg._angles.map((a, i) => ({ i, angle: 0, dur: 260 })));
      if (svg._subRing) svg._subRing.style.display = "";     // 未换位：平滑归位
    }
  };

  svg.addEventListener("pointerdown", (e) => {
    const g = e.target.closest("[data-sector]");
    if (!g || !svg._geom || svg._drag) return;
    const dg = Number(g.dataset.sector);
    const { deg, half } = centerPolar(e);
    svg._drag = { dg, startAngle: deg, lastAngle: deg, delta: 0,
                  moved: false, target: dg, slots: 0, half,
                  partner: null };
    window.addEventListener("pointermove", onMove, true);
    window.addEventListener("pointerup", onUp, true);
    window.addEventListener("pointercancel", onUp, true);
  });

  svg._getHover = () => hover;
}
