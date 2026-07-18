/*
 * virtual-keyboard.js — 手自笔录 触屏虚拟键盘
 *
 * 页面底部上拉展开的虚拟键盘：按当前布局表（qwerty / dvorak / dvorak4tzx /
 * 自定义 JSON 表）渲染键面，按键经 ime.sendKey 注入，与物理键盘走完全同一管线。
 *
 * 布局（左栏仿真实键盘左缘）：
 *   左栏:  功能列 `~ Tab ESC Shift Ctrl（5 行）
 *          + 数字/符号 2 列 × 8 矮行（双标签，末格 ▽ 收起键）
 *   主区:  字母三排（10/11/10，⌫ 在第一排右端、⏎ 在第三排右端）
 *          + 功能行（中/EN 切换、空格、方向键）
 *
 * 交互约定：
 *   - Shift / Ctrl = sticky：单击武装一次，下一个键带修饰发出后自动解除，再点自己取消
 *   - 中/EN 切换只走功能行专用键（键面实时显示当前状态）；sticky Shift 永不切语言
 *   - 长按 ← / → = Home / End；⌫ 长按自动重复
 *   - 触屏（coarse）下打开键盘时 textarea 置 readonly 屏蔽系统键盘，
 *     INSERT 模式改自绘细光标（ime touchCaret），文本区底部补 padding 防光标被遮
 *
 * 用法：
 *   const vkb = VirtualKeyboard.attach(ime, textarea, { coarse: true });
 *   vkb.setEnabled(true);    // 显示上拉手柄 / 恢复上次展开状态
 *   vkb.refresh();           // 布局等配置变化后重读布局表刷新键面
 *   vkb.destroy();
 */
(function (global) {
"use strict";

// 主区三排的 e.code 序列（与 ime-editor.js 的 LAYOUTS 键位一致；
// dvorak 系布局把字母放在标点键位上，所以标点键位也在字母排里按表渲染）
const ROW1 = ["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO", "KeyP"];
const ROW2 = ["KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL",
              "Semicolon", "Quote"];
const ROW3 = ["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM",
              "Comma", "Period", "Slash"];
// 左栏数字/符号两列（行优先，2 列 × 8 行；null = ▽ 收起键）
const SCOL = ["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8",
              "Digit9", "Digit0", "Minus", "Equal", "BracketLeft", "BracketRight", "Backslash",
              null];

const LS_KEY = "shouzibilu.vkb";

function attach(ime, textarea, opts) {
  const cfg = Object.assign({ coarse: false }, opts);

  let open = false;
  try { open = localStorage.getItem(LS_KEY) === "1"; } catch (e) { /* 隐私模式 */ }
  let enabled = false;
  let shiftArm = false, ctrlArm = false;
  let destroyed = false;

  // ---------- 样式（注入一次） ----------
  const style = document.createElement("style");
  style.textContent =
    ".vkb-handle{position:fixed;left:50%;transform:translateX(-50%);" +
      "bottom:calc(6px + env(safe-area-inset-bottom,0px));width:104px;height:26px;" +
      "border-radius:13px;background:rgba(90,100,110,.3);display:none;" +
      "align-items:center;justify-content:center;z-index:10001;cursor:pointer;" +
      "touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}" +
    ".vkb-handle-bar{width:44px;height:5px;border-radius:3px;background:rgba(255,255,255,.9)}" +
    ".vkb-panel{position:fixed;left:0;right:0;bottom:0;z-index:9000;display:none;" +
      "background:#cdd2d9;padding:6px 5px calc(6px + env(safe-area-inset-bottom,0px));" +
      "box-sizing:border-box;font-family:-apple-system,'PingFang SC',sans-serif;" +
      "user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;" +
      "touch-action:none;box-shadow:0 -1px 8px rgba(0,0,0,.18);transition:opacity .15s}" +
    ".vkb-faded{opacity:.12}" +
    ".vkb-body{display:flex;gap:5px;height:calc(4 * clamp(44px,7.5vh,64px) + 15px)}" +
    ".vkb-fcol{display:flex;flex-direction:column;gap:5px;flex:none}" +
    ".vkb-fcol .vkb-key{min-width:34px;font-size:11px;color:#222}" +
    ".vkb-scols{display:grid;grid-template-columns:repeat(2,1fr);" +
      "grid-template-rows:repeat(8,1fr);gap:5px;flex:none}" +
    ".vkb-scols .vkb-key{min-width:32px;font-size:11px}" +
    ".vkb-main{flex:1;display:flex;flex-direction:column;gap:5px;min-width:0}" +
    ".vkb-row{display:flex;gap:5px;flex:1}" +
    ".vkb-key{flex:1;background:#fdfdfd;border-radius:6px;box-shadow:0 1px 0 rgba(0,0,0,.35);" +
      "display:flex;align-items:center;justify-content:center;position:relative;" +
      "color:#111;font-size:17px;cursor:pointer}" +
    ".vkb-key:active{background:#aab2bd}" +
    ".vkb-func{background:#b9c0ca;font-size:12px}" +
    ".vkb-wide{flex:1.5}" +
    ".vkb-space{flex:4.2}" +
    ".vkb-armed{background:#4a90d9;color:#fff}" +
    ".vkb-armed .vkb-sub{color:#dce9f7}" +
    ".vkb-sub{position:absolute;top:1px;right:4px;font-size:8px;color:#777}";
  document.head.appendChild(style);

  // ---------- DOM ----------
  const handle = document.createElement("div");
  handle.className = "vkb-handle";
  handle.title = "上拉或点按展开虚拟键盘";
  const handleBar = document.createElement("div");
  handleBar.className = "vkb-handle-bar";
  handle.appendChild(handleBar);

  const panel = document.createElement("div");
  panel.className = "vkb-panel";
  const body = document.createElement("div");
  body.className = "vkb-body";
  panel.appendChild(body);
  document.body.appendChild(handle);
  document.body.appendChild(panel);

  // ---------- 键面渲染 ----------
  const letterKeys = [];   // [{el, code}] 主区三排
  const dualKeys = [];     // [{el, code}] 左栏双标签键（含 Backquote）
  let kShift = null, kCtrl = null, kLang = null;

  function tableEntry(code) {
    const t = ime.getLayoutTable();
    const qw = global.ImeEditor && global.ImeEditor.LAYOUTS
      ? global.ImeEditor.LAYOUTS.qwerty : null;
    return (t && t[code]) || (qw && qw[code]) || null;
  }

  function refreshLabels() {
    for (const k of letterKeys) {
      const en = tableEntry(k.code);
      k.el.textContent = en ? (shiftArm ? en[1] : en[0]) : "";
    }
    for (const k of dualKeys) {
      const en = tableEntry(k.code);
      k.el.textContent = "";
      if (!en) continue;
      const big = document.createElement("span");
      big.textContent = shiftArm ? en[1] : en[0];
      k.el.appendChild(big);
      if (en[1] !== en[0]) {
        const sub = document.createElement("span");
        sub.className = "vkb-sub";
        sub.textContent = shiftArm ? en[0] : en[1];
        k.el.appendChild(sub);
      }
    }
    if (kShift) kShift.classList.toggle("vkb-armed", shiftArm);
    if (kCtrl) kCtrl.classList.toggle("vkb-armed", ctrlArm);
    refreshLang();
  }
  function refreshLang() {
    if (kLang) kLang.textContent = ime.getMode().english ? "EN" : "中";
  }

  // ---------- 发键 ----------
  function afterFire() {   // sticky 修饰单发：任何真实键发出后解除
    if (shiftArm || ctrlArm) { shiftArm = ctrlArm = false; refreshLabels(); }
    refreshLang();
  }
  function fireCode(code) {
    const en = tableEntry(code);
    if (!en) { if (code === "Space") fireNamed(" ", "Space"); return; }
    ime.sendKey({
      key: shiftArm ? en[1] : en[0],
      code, shiftKey: shiftArm, ctrlKey: ctrlArm,
    });
    afterFire();
  }
  function fireNamed(key, code) {
    ime.sendKey({ key, code: code || "", shiftKey: shiftArm, ctrlKey: ctrlArm });
    afterFire();
  }
  function fireLang() {
    ime.sendKey({ key: "Shift" });     // keydown: 置 shiftLone
    ime.sendKeyUp({ key: "Shift" });   // keyup: 翻转 english（INSERT/SEARCH 内生效）
    refreshLang();
  }

  // ---------- 绑定 ----------
  function pd(el, fn) {
    el.addEventListener("pointerdown", (e) => { e.preventDefault(); fn(e); });
  }
  function mkKey(parent, cls) {
    const el = document.createElement("div");
    el.className = "vkb-key" + (cls ? " " + cls : "");
    parent.appendChild(el);
    return el;
  }

  // 左栏功能列：`~ Tab ESC Shift Ctrl（真实键盘左缘）
  const fcol = document.createElement("div");
  fcol.className = "vkb-fcol";
  body.appendChild(fcol);
  const kBackq = mkKey(fcol, "vkb-func");
  dualKeys.push({ el: kBackq, code: "Backquote" });
  pd(kBackq, () => fireCode("Backquote"));
  const kTab = mkKey(fcol, "vkb-func"); kTab.textContent = "Tab";
  pd(kTab, () => fireNamed("Tab", "Tab"));
  const kEsc = mkKey(fcol, "vkb-func"); kEsc.textContent = "ESC";
  pd(kEsc, () => fireNamed("Escape", "Escape"));
  kShift = mkKey(fcol, "vkb-func"); kShift.textContent = "⇧";
  pd(kShift, () => { shiftArm = !shiftArm; refreshLabels(); });
  kCtrl = mkKey(fcol, "vkb-func"); kCtrl.textContent = "Ctrl";
  pd(kCtrl, () => { ctrlArm = !ctrlArm; refreshLabels(); });

  // 左栏数字/符号两列（8 矮行）+ ▽ 收起
  const scols = document.createElement("div");
  scols.className = "vkb-scols";
  body.appendChild(scols);
  for (const code of SCOL) {
    if (code) {
      const k = mkKey(scols, "vkb-func");
      dualKeys.push({ el: k, code });
      pd(k, () => fireCode(code));
    } else {
      const k = mkKey(scols, "vkb-func");
      k.textContent = "▽";
      k.title = "收起键盘";
      pd(k, () => closeKb());
    }
  }

  // 主区
  const main = document.createElement("div");
  main.className = "vkb-main";
  body.appendChild(main);
  function letterRow(codes, parent) {
    for (const code of codes) {
      const k = mkKey(parent, "");
      letterKeys.push({ el: k, code });
      pd(k, () => fireCode(code));
    }
  }
  // 第 1 排：字母 + ⌫（长按自动重复）
  const r1 = document.createElement("div"); r1.className = "vkb-row"; main.appendChild(r1);
  letterRow(ROW1, r1);
  const kBksp = mkKey(r1, "vkb-func vkb-wide"); kBksp.textContent = "⌫";
  let repTimer = null;
  kBksp.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    fireNamed("Backspace", "Backspace");
    clearTimeout(repTimer);
    repTimer = setTimeout(function rep() {
      fireNamed("Backspace", "Backspace");
      repTimer = setTimeout(rep, 55);
    }, 380);
  });
  for (const ev of ["pointerup", "pointercancel", "pointerleave"]) {
    kBksp.addEventListener(ev, () => clearTimeout(repTimer));
  }
  // 第 2 排：字母（11 键）
  const r2 = document.createElement("div"); r2.className = "vkb-row"; main.appendChild(r2);
  letterRow(ROW2, r2);
  // 第 3 排：字母 + ⏎
  const r3 = document.createElement("div"); r3.className = "vkb-row"; main.appendChild(r3);
  letterRow(ROW3, r3);
  const kEnter = mkKey(r3, "vkb-func vkb-wide"); kEnter.textContent = "⏎";
  pd(kEnter, () => fireNamed("Enter", "Enter"));
  // 第 4 排：中/EN、空格、方向键（←/→ 长按 = Home/End）
  const r4 = document.createElement("div"); r4.className = "vkb-row"; main.appendChild(r4);
  kLang = mkKey(r4, "vkb-func");
  pd(kLang, fireLang);
  const kSpace = mkKey(r4, "vkb-space");
  pd(kSpace, () => fireCode("Space"));
  function bindArrowLP(el, arrow, homeEnd) {
    let timer = null, lpFired = false, canceled = false;
    el.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      lpFired = canceled = false;
      timer = setTimeout(() => {
        lpFired = true;
        if (navigator.vibrate) navigator.vibrate(10);
        fireNamed(homeEnd);
      }, 500);
    });
    el.addEventListener("pointerup", () => {
      clearTimeout(timer);
      if (!lpFired && !canceled) fireNamed(arrow);
    });
    for (const ev of ["pointercancel", "pointerleave"]) {
      el.addEventListener(ev, () => { canceled = true; clearTimeout(timer); });
    }
  }
  const kLeft = mkKey(r4, "vkb-func"); kLeft.textContent = "←";
  bindArrowLP(kLeft, "ArrowLeft", "Home");
  const kUp = mkKey(r4, "vkb-func"); kUp.textContent = "↑";
  pd(kUp, () => fireNamed("ArrowUp"));
  const kDown = mkKey(r4, "vkb-func"); kDown.textContent = "↓";
  pd(kDown, () => fireNamed("ArrowDown"));
  const kRight = mkKey(r4, "vkb-func"); kRight.textContent = "→";
  bindArrowLP(kRight, "ArrowRight", "End");

  // ---------- 开合 ----------
  function persist() {
    try { localStorage.setItem(LS_KEY, open ? "1" : "0"); } catch (e) { /* ignore */ }
  }
  function applyVisibility() {
    handle.style.display = enabled && !open ? "flex" : "none";
    panel.style.display = enabled && open ? "flex" : "none";
  }
  const basePadBottom = parseFloat(getComputedStyle(textarea).paddingBottom) || 0;
  function applyTextarea() {
    if (destroyed) return;
    textarea.style.paddingBottom = (basePadBottom + panel.offsetHeight) + "px";
    if (cfg.coarse) {
      textarea.readOnly = true;              // 屏蔽系统键盘（iOS/Android 都吃 readonly）
      ime.setOption({ touchCaret: true });   // readonly 下无原生光标，改自绘细光标
    }
    textarea.focus();
    window.dispatchEvent(new Event("resize"));   // 触发编辑器行号/高亮/光标刷新
  }
  function unapplyTextarea() {
    textarea.style.paddingBottom = "";
    if (cfg.coarse) {
      textarea.readOnly = false;
      ime.setOption({ touchCaret: false });
    }
    window.dispatchEvent(new Event("resize"));
  }
  function openKb() {
    if (open) return;
    open = true; persist(); applyVisibility();
    requestAnimationFrame(applyTextarea);   // 等 panel 渲染后量高
  }
  function closeKb() {
    if (!open) return;
    open = false; persist(); applyVisibility(); unapplyTextarea();
  }

  // 手柄：点按 / 上拉展开
  let hStartY = null;
  handle.addEventListener("pointerdown", (e) => { e.preventDefault(); hStartY = e.clientY; });
  handle.addEventListener("pointermove", (e) => {
    if (hStartY != null && hStartY - e.clientY > 24) { hStartY = null; openKb(); }
  });
  handle.addEventListener("pointerup", () => {
    if (hStartY != null) { hStartY = null; openKb(); }
  });
  handle.addEventListener("pointercancel", () => { hStartY = null; });
  // 面板背景（非键区）下拉收起；面板整体 preventDefault 防焦点转移
  let pStartY = null;
  panel.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    pStartY = e.target.closest(".vkb-key") ? null : e.clientY;
  });
  panel.addEventListener("pointermove", (e) => {
    if (pStartY != null && e.clientY - pStartY > 40) { pStartY = null; closeKb(); }
  });
  panel.addEventListener("pointerup", () => { pStartY = null; });
  panel.addEventListener("pointercancel", () => { pStartY = null; });
  panel.addEventListener("contextmenu", (e) => e.preventDefault());

  // 横竖屏/窗口变化：键盘高随 vh 变，重算文本区底部 padding
  function onResize() {
    if (enabled && open) {
      textarea.style.paddingBottom = (basePadBottom + panel.offsetHeight) + "px";
    }
  }
  window.addEventListener("resize", onResize);

  refreshLabels();
  applyVisibility();

  return {
    setEnabled(b) {
      enabled = !!b;
      applyVisibility();
      if (enabled && open) requestAnimationFrame(applyTextarea);
      else if (!enabled && open) unapplyTextarea();
    },
    refresh: refreshLabels,
    isOpen: () => enabled && open,
    destroy() {
      destroyed = true;
      if (enabled && open) unapplyTextarea();
      window.removeEventListener("resize", onResize);
      clearTimeout(repTimer);
      handle.remove(); panel.remove(); style.remove();
    },
  };
}

const VirtualKeyboard = { attach };
if (typeof module !== "undefined" && module.exports) module.exports = VirtualKeyboard;
global.VirtualKeyboard = VirtualKeyboard;
})(typeof window !== "undefined" ? window : globalThis);
