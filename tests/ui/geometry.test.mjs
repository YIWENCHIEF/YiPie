import test from "node:test";
import assert from "node:assert/strict";
import { sectorIndex, sectorAngles, arcWalk, annularSectorPath, labelAnchor, MAX_LABEL_SECTORS } from "../../ui/geometry.mjs";

const TAU = Math.PI * 2;

test("sectorIndex: east=0, south=2 (n=8), north=6, NE=7", () => {
  assert.equal(sectorIndex(0, 8), 0);
  assert.equal(sectorIndex(Math.PI / 2, 8), 2);            // +y 向下 = 南
  assert.equal(sectorIndex(3 * Math.PI / 2, 8), 6);        // 北
  assert.equal(sectorIndex(-Math.PI / 4, 8), 7);           // 东北 315°（引擎 Task2 同款输入）
});

test("sectorIndex: 45° 对角归逆时针前一扇区（引擎 ceil 约定）n=4", () => {
  assert.equal(sectorIndex(-Math.PI / 4, 4), 3);           // (30,-30) 类
  assert.equal(sectorIndex(3 * Math.PI / 4, 4), 1);        // (-30,30) 类
});

test("sectorIndex: 边界外角度归一", () => {
  assert.equal(sectorIndex(-Math.PI / 2, 8), 6);           // 负角归一 = 北
  assert.equal(sectorIndex(TAU * 3 + 0.1, 8), 0);
});

test("sectorIndex: n=3 与 n=16 合法域", () => {
  for (let i = 0; i < 1000; i++) {
    const th = Math.random() * TAU;
    const s3 = sectorIndex(th, 3);
    assert.ok(s3 >= 0 && s3 < 3);
    const s16 = sectorIndex(th, 16);
    assert.ok(s16 >= 0 && s16 < 16);
  }
});

test("arcWalk: 120° 拆 2 段且每段 ≤90°", () => {
  const pts = arcWalk(0, TAU / 3, true);
  assert.equal(pts.length, 3);                              // 起点+1 中间点+终点
  assert.ok(pts[2] - pts[1] <= Math.PI / 2 + 1e-9);
  assert.ok(pts[1] - pts[0] <= Math.PI / 2 + 1e-9);
});

test("arcWalk: CCW 递减且步数正确", () => {
  const pts = arcWalk(TAU / 3, 0, false);
  assert.equal(pts.length, 3);
  assert.ok(pts[1] < pts[0] && pts[2] < pts[1]);
});

test("arcWalk: 小角度单段", () => {
  const pts = arcWalk(0, 0.5, true);
  assert.equal(pts.length, 2);
});

test("annularSectorPath: M/A/L/A/Z 结构、sweep 正确、largeArc=0", () => {
  const d = annularSectorPath(100, 100, 52, 138, -0.6, 0.6);
  assert.match(d, /^M .+ A .+ 0 0 1 .+ L .+ A .+ 0 0 0 .+ Z$/);   // 外 CW(sweep1)/内 CCW(sweep0)
  assert.ok(!/ 1 [01] /.test(d));                                   // 无 largeArc=1
});

test("annularSectorPath: 120° 大扇区仍全 small-arc", () => {
  const d = annularSectorPath(210, 210, 70, 180, 0, TAU / 3);
  assert.ok(!/ 1 [01] /.test(d));
  // 外弧 2 段 + 内弧 2 段 = 4 个 A 命令
  assert.equal((d.match(/ A /g) || []).length, 4);
});

test("labelAnchor: 东向扇区在 x 轴正方向", () => {
  const [x, y] = labelAnchor(100, 100, 52, 138, 0);
  assert.ok(x > 100 && Math.abs(y - 100) < 1e-9);
});

test("sectorAngles: gap 内缩两侧", () => {
  const { a0, a1, mid } = sectorAngles(0, 8);
  assert.equal(mid, 0);
  assert.ok(a1 - a0 < TAU / 8);
  assert.ok(a0 < 0 && a1 > 0);
});

test("MAX_LABEL_SECTORS 与渲染器 kMaxLabelSectors 对齐", () => {
  assert.equal(MAX_LABEL_SECTORS, 12);
});

// ---- M3b: 子环角度均分 ----
test("subSectorAngles: 3 子槽均分东扇区，首尾内缩、中线对齐父中线", async () => {
  const { subSectorAngles, sectorAngles } = await import("../../ui/geometry.mjs");
  const parent = sectorAngles(0, 8);          // 东扇区 [a0,a1]
  const subs = subSectorAngles(0, 8, 3);
  assert.equal(subs.length, 3);
  const gap = 0.012;
  assert.ok(Math.abs(subs[0].a0 - (parent.a0 + gap / 2)) < 1e-9);
  assert.ok(Math.abs(subs[2].a1 - (parent.a1 - gap / 2)) < 1e-9);
  // 相邻子槽之间留 gap 间隙（与渲染器一致）
  assert.ok(Math.abs((subs[1].a0 - subs[0].a1) - gap) < 1e-9);
  // 子槽总数为奇数 -> 中间子槽中线 = 父中线
  assert.ok(Math.abs(subs[1].mid - parent.mid) < 1e-9);
  for (const s of subs) assert.ok(s.a1 > s.a0);
});

test("subSectorAngles: 1 子槽 = 父区间内缩；4 子槽均分", async () => {
  const { subSectorAngles, sectorAngles } = await import("../../ui/geometry.mjs");
  const one = subSectorAngles(3, 8, 1);
  const p3 = sectorAngles(3, 8);
  assert.equal(one.length, 1);
  assert.ok(Math.abs(one[0].a0 - (p3.a0 + 0.006)) < 1e-9);
  assert.ok(Math.abs(one[0].a1 - (p3.a1 - 0.006)) < 1e-9);
  const four = subSectorAngles(0, 4, 4);
  assert.equal(four.length, 4);
  assert.ok(Math.abs(four[3].a1 - (sectorAngles(0, 4).a1 - 0.006)) < 1e-9);
});
