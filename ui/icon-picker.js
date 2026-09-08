// M3c T3: 图标选择器浮层。锚点下方弹出：自动 / 字形网格 / 已导入列表 / 导入按钮。
// 导入 SVG/PNG 在前端光栅化为 64px PNG base64 后走 saveIcon（C++ 零 SVG 依赖）。
import { ipc, toast } from "./app.js";
import { loadGlyphs } from "./wheel-preview.js";

const importedThumbs = new Map();  // name -> dataURL（saveIcon/readIcon 后填充）

export async function openIconPicker(anchorEl, currentIconKey, onPick) {
  document.querySelectorAll(".icon-pop").forEach((p) => p.remove());
  const glyphs = await loadGlyphs();
  const pop = document.createElement("div");
  pop.className = "icon-pop";

  const mk = (html) => { const d = document.createElement("div"); d.innerHTML = html; return d; };

  // 自动
  const autoRow = mk(`<div class="icon-sec-title">自动（按动作类型/目标匹配）</div>`);
  const autoBtn = document.createElement("button");
  autoBtn.className = "ghost"; autoBtn.textContent = "使用自动图标";
  autoBtn.addEventListener("click", () => { onPick(""); close(); });
  autoRow.appendChild(autoBtn);
  pop.appendChild(autoRow);

  // 字形网格
  const gTitle = mk(`<div class="icon-sec-title">字形图标</div>`);
  pop.appendChild(gTitle);
  const grid = document.createElement("div");
  grid.className = "icon-grid";
  for (const [id, e] of Object.entries(glyphs)) {
    const cell = document.createElement("button");
    cell.className = "icon-cell" + (currentIconKey === "g:" + id ? " on" : "");
    cell.title = e.label;
    cell.textContent = String.fromCodePoint(parseInt(e.cp, 16));
    cell.style.fontFamily = '"Segoe MDL2 Assets"';
    cell.addEventListener("click", () => { onPick("g:" + id); close(); });
    grid.appendChild(cell);
  }
  pop.appendChild(grid);

  // 已导入
  const iTitle = mk(`<div class="icon-sec-title">已导入</div>`);
  pop.appendChild(iTitle);
  const impRow = document.createElement("div");
  impRow.className = "icon-grid";
  pop.appendChild(impRow);
  try {
    const list = await ipc("icons", { op: "list" });
    if (!list.files || !list.files.length) {
      impRow.appendChild(mk(`<span class="hint">（暂无，点右侧按钮导入）</span>`));
    }
    for (const f of list.files) {
      const cell = document.createElement("button");
      cell.className = "icon-cell img" + (currentIconKey === "f:" + f ? " on" : "");
      cell.title = f;
      const img = await thumbFor(f);
      if (img) cell.appendChild(img);
      cell.addEventListener("click", () => { onPick("f:" + f); close(); });
      cell.addEventListener("contextmenu", async (e) => {
        e.preventDefault();
        if (!confirm(`删除图标 ${f}？`)) return;
        const r = await ipc("icons", { op: "delete", name: f });
        if (r.inUse) { toast("该图标正被扇区引用，无法删除", true); return; }
        importedThumbs.delete(f);
        toast("已删除");
        openIconPicker(anchorEl, currentIconKey, onPick);  // 重建列表
      });
      impRow.appendChild(cell);
    }
  } catch (err) {
    impRow.appendChild(mk(`<span class="hint">列表读取失败：${err.message}</span>`));
  }

  // 导入按钮
  const importBtn = mk(`<div class="icon-sec-title"></div>`);
  const btn = document.createElement("button");
  btn.className = "ghost"; btn.textContent = "导入图片…（SVG/PNG）";
  btn.addEventListener("click", () => fileInput.click());
  const fileInput = document.createElement("input");
  fileInput.type = "file"; fileInput.accept = ".svg,.png";
  fileInput.style.display = "none";
  fileInput.addEventListener("change", async () => {
    const file = fileInput.files[0];
    if (!file) return;
    try {
      const b64 = await rasterizeToPng64(file);
      let safeName = (file.name.replace(/\.[^.]+$/, "") || "icon")
        .replace(/[^A-Za-z0-9_.-]/g, "_").slice(0, 48) + ".png";
      const r = await ipc("saveIcon", { name: safeName, base64Png: b64 });
      importedThumbs.set(safeName, "data:image/png;base64," + b64);
      toast("图标已导入");
      onPick(r.iconKey);
      close();
    } catch (err) {
      toast("导入失败：" + err.message, true);
    }
    fileInput.value = "";
  });
  importBtn.appendChild(btn);
  importBtn.appendChild(fileInput);
  pop.appendChild(importBtn);

  function close() { pop.remove(); document.removeEventListener("mousedown", outside, true); }
  function outside(e) { if (!pop.contains(e.target) && !anchorEl.contains(e.target)) close(); }

  document.body.appendChild(pop);
  const rect = anchorEl.getBoundingClientRect();
  pop.style.left = Math.min(rect.left, window.innerWidth - 340) + "px";
  pop.style.top = (rect.bottom + 6) + "px";
  setTimeout(() => document.addEventListener("mousedown", outside, true), 0);
}

async function thumbFor(name) {
  try {
    let url = importedThumbs.get(name);
    if (!url) {
      const r = await ipc("readIcon", { name });
      url = "data:image/png;base64," + r.base64;
      importedThumbs.set(name, url);
    }
    const img = document.createElement("img");
    img.src = url; img.width = 22; img.height = 22;
    return img;
  } catch { return null; }
}

// SVG/PNG -> 64x64 PNG base64（canvas 光栅化；SVG 走 <img> 加载 data URL）
async function rasterizeToPng64(file) {
  const dataUrl = await new Promise((res, rej) => {
    const fr = new FileReader();
    fr.onload = () => res(fr.result);
    fr.onerror = () => rej(new Error("文件读取失败"));
    fr.readAsDataURL(file);
  });
  const img = await new Promise((res, rej) => {
    const i = new Image();
    i.onload = () => res(i);
    i.onerror = () => rej(new Error("图片解码失败"));
    i.src = dataUrl;
  });
  const cv = document.createElement("canvas");
  cv.width = 64; cv.height = 64;
  const ctx = cv.getContext("2d");
  ctx.drawImage(img, 0, 0, 64, 64);
  return cv.toDataURL("image/png").split(",")[1];
}
