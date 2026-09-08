import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..", "..");

test("glyphs.json: 合法 JSON、35 个 id、cp 为 4 位 hex", () => {
  const g = JSON.parse(readFileSync(join(root, "ui", "glyphs.json"), "utf8"));
  const ids = Object.keys(g);
  assert.equal(ids.length, 35);
  for (const id of ids) {
    assert.match(g[id].cp, /^[0-9A-F]{4}$/, id + " cp 必须是 4 位大写 hex");
    assert.ok(typeof g[id].label === "string" && g[id].label.length > 0);
    assert.ok(Array.isArray(g[id].patterns));
  }
  // 关键 id 齐全
  for (const need of ["copy", "paste", "lock", "browser", "app", "camera", "window"])
    assert.ok(ids.includes(need), "缺 " + need);
});

test("glyphs.json: pattern 全局唯一（无跨字形冲突）", () => {
  const g = JSON.parse(readFileSync(join(root, "ui", "glyphs.json"), "utf8"));
  const seen = new Map();
  for (const [id, e] of Object.entries(g))
    for (const p of e.patterns) {
      assert.ok(!seen.has(p), `pattern "${p}" 同时属于 ${seen.get(p)} 和 ${id}`);
      seen.set(p, id);
    }
});

test("生成头与 JSON 一致（count + 每 id + 码点）", () => {
  const g = JSON.parse(readFileSync(join(root, "ui", "glyphs.json"), "utf8"));
  let hdr;
  try {
    hdr = readFileSync(join(root, "src", "glyph_table.generated.h"), "utf8");
  } catch {
    // 生成头由 cmake 构建产生；测试环境先跑一次生成脚本
    console.error("glyph_table.generated.h 不存在——先运行 scripts/gen-glyph-header.ps1 或 cmake 构建");
    throw new Error("missing generated header");
  }
  assert.ok(hdr.includes(`kGlyphCount = ${Object.keys(g).length}`));
  for (const [id, e] of Object.entries(g)) {
    assert.ok(hdr.includes(`"${id}"`), "头缺 id " + id);
    assert.ok(hdr.includes(`0x${e.cp}`), "头缺码点 " + id);
  }
});
