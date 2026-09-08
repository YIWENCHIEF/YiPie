// 配置补丁纯逻辑（M2 收尾波次）。DOM-free，node --test 可测。
// app.js / page-settings.js 共用，消除重复实现。

export function isPlainObject(v) {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

// base=权威/累积补丁, patch=新补丁。数组与标量以 patch 为准，对象递归合并。
export function deepMerge(base, patch) {
  if (patch === undefined) return structuredClone(base);
  if (!isPlainObject(base) || !isPlainObject(patch)) return structuredClone(patch);
  const out = {};
  for (const k of Object.keys(base)) out[k] = deepMerge(base[k], patch[k]);
  for (const k of Object.keys(patch)) if (!(k in out)) out[k] = structuredClone(patch[k]);
  return out;
}

// "gesture.dragThreshold" -> {gesture:{dragThreshold:v}}
export function patchOf(path, value) {
  const keys = path.split(".");
  let v = value;
  for (let i = keys.length - 1; i > 0; i--) v = { [keys[i]]: v };
  return { [keys[0]]: v };
}

export function getPath(o, path) {
  return path.split(".").reduce((x, k) => (x == null ? undefined : x[k]), o);
}

// 防抖期间累积补丁：后写覆盖同路径，不同路径都保留。
// 返回新的累积补丁（pending 为 null 时返回 partial 的深拷贝）。
export function accumulatePatch(pending, partial) {
  return pending ? deepMerge(pending, partial) : structuredClone(partial);
}
