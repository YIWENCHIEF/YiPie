// YiPie 轮盘几何（M2 T4）—— DOM-free，可被 node --test 直接验证。
// 必须与 src/gesture_engine.cpp + src/wheel_window.cpp 的判定/绘制语义一致：
//  * 扇区 0 = 正东（角度 0），顺时针（屏幕 y 向下），边界归属 ceil(θ/step − 0.5)
//    （恰在扇区线上归逆时针前一扇区 —— 引擎 Task 2 的已裁定约定）
//  * 弧隙 0.012 rad；每段 AddArc ≤90°（n=3 时扇区 120°，单段 A 命令画不了）
//  * 标签仅在 n ≤ 12 时绘制（渲染器 kMaxLabelSectors）
export const MAX_LABEL_SECTORS = 12;
export const ARC_GAP_RAD = 0.012;
const TAU = Math.PI * 2;

function normAngle(theta) {
  let t = theta % TAU;
  if (t < 0) t += TAU;
  return t;
}

// 光标方位角 -> 扇区索引。与引擎 ProcessMove 的 ceil 式一致。
export function sectorIndex(theta, n) {
  if (n <= 0) return -1;
  const t = normAngle(theta);
  const step = TAU / n;
  let idx = Math.ceil(t / step - 0.5);
  idx = ((idx % n) + n) % n;
  return idx;
}

// 扇区 i 的绘制角度区间（含弧隙内缩）与中线角。
export function sectorAngles(i, n, gap = ARC_GAP_RAD) {
  const step = TAU / n;
  const mid = i * step;
  return { a0: mid - step / 2 + gap, a1: mid + step / 2 - gap, mid };
}

// 从 from 走到 to（cw=true 递增 / false 递减），每步 ≤90°，返回含端点的角度数组。
// 与 wheel_window.cpp AddArcSplit 的 steps=ceil(span/90°) 语义一致。
export function arcWalk(from, to, cw, stepMax = Math.PI / 2) {
  const span = Math.max(0, cw ? to - from : from - to);
  const steps = Math.max(1, Math.ceil(span / stepMax - 1e-9));
  const pts = [from];
  for (let i = 1; i <= steps; i++) {
    pts.push(cw ? from + (span / steps) * i : from - (span / steps) * i);
  }
  return pts;
}

const fmt = (v) => Number(v.toFixed(6)).toString();

function pt(cx, cy, r, a) {
  return [fmt(cx + r * Math.cos(a)), fmt(cy + r * Math.sin(a))];
}

// 环形扇区 SVG path：外弧 CW(sweep=1) -> 直线到内弧端 -> 内弧 CCW(sweep=0) -> 闭合。
// 每段 ≤90° 故 largeArc 恒为 0。
export function annularSectorPath(cx, cy, rInner, rOuter, a0, a1) {
  const segs = [];
  // 外弧 a0 -> a1 顺时针
  const outerPts = arcWalk(a0, a1, true);
  segs.push("M " + pt(cx, cy, rOuter, outerPts[0]).join(" "));
  for (let i = 1; i < outerPts.length; i++) {
    const [x, y] = pt(cx, cy, rOuter, outerPts[i]);
    segs.push("A " + fmt(rOuter) + " " + fmt(rOuter) + " 0 0 1 " + x + " " + y);
  }
  // 直线到内弧 a1 处
  const [lx, ly] = pt(cx, cy, rInner, a1);
  segs.push("L " + lx + " " + ly);
  // 内弧 a1 -> a0 逆时针 (sweep=0)
  const innerPts = arcWalk(a1, a0, false);
  for (let i = 1; i < innerPts.length; i++) {
    const [x, y] = pt(cx, cy, rInner, innerPts[i]);
    segs.push("A " + fmt(rInner) + " " + fmt(rInner) + " 0 0 0 " + x + " " + y);
  }
  segs.push("Z");
  return segs.join(" ");
}

// 标签锚点：扇区中线、内外半径中点。
export function labelAnchor(cx, cy, rInner, rOuter, mid) {
  const r = (rInner + rOuter) / 2;
  return [cx + r * Math.cos(mid), cy + r * Math.sin(mid)];
}

// 子环扇叶角度：父扇区区间（含 gap 内缩）按 m 均分，子槽之间再留 gap 间隙。
// 与渲染器 wheel_window.cpp 子环绘制块语义一致（改动需双侧同步）。
export function subSectorAngles(parentIndex, n, subCount, gap = ARC_GAP_RAD) {
  const { a0, a1 } = sectorAngles(parentIndex, n, gap);
  const m = Math.max(1, Math.min(4, subCount));
  const span = (a1 - a0) / m;
  const out = [];
  for (let j = 0; j < m; j++) {
    const s0 = a0 + span * j + gap / 2;
    const s1 = a0 + span * (j + 1) - gap / 2;
    out.push({ a0: s0, a1: s1, mid: (s0 + s1) / 2 });
  }
  return out;
}
