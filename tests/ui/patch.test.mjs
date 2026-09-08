import test from "node:test";
import assert from "node:assert/strict";
import { deepMerge, patchOf, getPath, accumulatePatch } from "../../ui/patch.mjs";

test("patchOf: 嵌套路径构造补丁", () => {
  assert.deepEqual(patchOf("gesture.dragThreshold", 30), { gesture: { dragThreshold: 30 } });
  assert.deepEqual(patchOf("appearance.theme", "light"), { appearance: { theme: "light" } });
});

test("getPath: 嵌套路径读取", () => {
  const o = { gesture: { dragThreshold: 30 } };
  assert.equal(getPath(o, "gesture.dragThreshold"), 30);
  assert.equal(getPath(o, "gesture.missing"), undefined);
});

test("deepMerge: 数组以 patch 为准，对象递归，缺失保留 base", () => {
  const base = { a: 1, obj: { x: 1, y: 2 }, arr: [1, 2, 3] };
  const merged = deepMerge(base, { obj: { y: 9 }, arr: [7] });
  assert.deepEqual(merged, { a: 1, obj: { x: 1, y: 9 }, arr: [7] });
});

// 评审 Important #2 的回归：防抖窗口内先后改两个不同字段，两者都必须存活
test("accumulatePatch: 不同路径的补丁都保留（防抖不丢字段）", () => {
  let p = null;
  p = accumulatePatch(p, { profiles: [{ processName: "Global", actions: [{ name: "复制" }] }] });
  p = accumulatePatch(p, { appearance: { opacity: "low" } });
  assert.equal(p.appearance.opacity, "low");
  assert.equal(p.profiles[0].actions[0].name, "复制");
});

test("accumulatePatch: 同路径后写覆盖", () => {
  let p = accumulatePatch(null, { gesture: { dragThreshold: 30 } });
  p = accumulatePatch(p, { gesture: { dragThreshold: 40 } });
  assert.equal(p.gesture.dragThreshold, 40);
});

test("accumulatePatch: 同槽位连续编辑按字段合并", () => {
  let p = accumulatePatch(null, { profiles: [{ processName: "Global", actions: [{ name: "记事本" }] }] });
  p = accumulatePatch(p, { profiles: [{ processName: "Global", actions: [{ target: "notepad.exe" }] }] });
  // 数组整体覆盖语义：actions[0] 只剩 target —— 与 C++ 端"整份配置替换"一致，
  // 抽屉的 commit 每次都带完整槽位对象，所以实际链路不触发该覆盖。
  assert.deepEqual(p.profiles[0].actions[0], { target: "notepad.exe" });
});
