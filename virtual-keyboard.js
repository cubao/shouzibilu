/*
 * virtual-keyboard.js — 手自笔录 触屏虚拟键盘
 *
 * 页面底部上拉展开的虚拟键盘：键面随布局表（qwerty / dvorak / dvorak4tzx /
 * 自定义 JSON 表）渲染，按键经 ime.sendKey 注入，与物理键盘走完全同一管线。
 *
 * 布局（六排，仿真实键盘轮廓 + ANSI 错行）：
 *   第0排:  ▽    ! @ # $ % ^ & * ( ) { } [ ] +   （shifted 数字符号+补充符号，直按）
 *   第A排:  `~   1 2 3 4 5 6 7 8 9 0 \ | = _ -   （数字+补充符号，直按）
 *   第1排:  Tab     q w e r t y u i o p  ⌫        （字母按布局表；q 与数字 1 左缘对齐）
 *   第2排:  ESC     a s d f g h j k l ;  '        （右错 2%）
 *   第3排:  ⇧       z x c v b n m , . /  ⏎        （右错 4.5%）
 *   （第0/A 排高度 = 字母排的 60%，功能排 = 80%）
 *   第4排:  Ctrl 中/EN 👆 [—— 空格 ——] ← ↑ ↓ →
 *   ; ' , . / 是常规字母区键位，随布局表整体翻译（dvorak 系下出字母）。
 *
 * 交互约定：
 *   - ⇧ / Ctrl = sticky：单击武装一次，下一个键带修饰发出后自动解除，再点自己取消；
 *     武装只影响字母大小写与 `~ 键（数字/符号排全部直按，无视武装）
 *   - 中/EN 切换只走功能行专用键（键面实时显示当前状态，物理 Shift 切换也同步）；sticky Shift 永不切语言
 *   - 双拼方案下字母键标注韵母（浅绿、右下角小字，zh/ch/sh 灰字标在 v/i/u 左上）
 *   - 长按 ← / → = Home / End；⌫ 长按自动重复；👆 长按 = 放大镜拖动光标
 *   - 触屏（coarse）下打开键盘时 textarea 置 readonly 屏蔽系统键盘，
 *     INSERT 模式改自绘细光标（ime touchCaret），文本区底部补 padding 防光标被遮
 *
 * 用法：
 *   const vkb = VirtualKeyboard.attach(ime, textarea, {
 *     coarse: true,
 *     getShuangpinFinals: () => ({ q: ["iu"], ... } | null),   // 可选
 *     onToggle: (open, kbHeight) => {},   // 可选：展开/收起回调（页面可借此隐藏杂物、收底）
 *     padContent: true,                   // 可选：展开时给 textarea 内容加底部 padding
 *   });                                   // （页面用 onToggle 自收容器时可关）
 *   vkb.setEnabled(true);    // 显示上拉手柄 / 恢复上次展开状态
 *   vkb.refresh();           // 布局/方案等配置变化后重读配置刷新键面
 *   vkb.destroy();
 */
(function (global) {
"use strict";

// 字母三排的 e.code 序列（与 ime-editor.js 的 LAYOUTS 键位一致；
// dvorak 系布局把字母放在标点键位上，; ' , . / 随布局表整体翻译）
const ROW1 = ["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO", "KeyP"];
const ROW2 = ["KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK", "KeyL",
              "Semicolon", "Quote"];
const ROW3 = ["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM",
              "Comma", "Period", "Slash"];
// 第0排：shifted 数字符号 + 补充符号（直按）；第A排：数字 + 补充符号（直按）
const SYM_ROW0 = "! @ # $ % ^ & * ( ) { } [ ] +".split(" ");
const SYM_ROWA = "1 2 3 4 5 6 7 8 9 0 \\ | = _ -".split(" ");
// 字母排错行幅度（行宽百分比，ANSI 错落比例）
const STAGGER = ["0%", "2%", "4.5%"];   // 首排归零：q 与数字 1 左缘对齐
// 双拼声母键提示（zh/ch/sh = v/i/u）
const SP_HINT = { v: "zh", i: "ch", u: "sh" };

const LS_KEY = "shouzibilu.vkb";

function attach(ime, textarea, opts) {
  const cfg = Object.assign({
    coarse: false,
    getShuangpinFinals: () => null,
    onToggle: () => {},
    padContent: true,
  }, opts);

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
      "background:#cdd2d9;padding:6px 0 calc(6px + env(safe-area-inset-bottom,0px));" +
      "box-sizing:border-box;font-family:-apple-system,'PingFang SC',sans-serif;" +
      "user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;" +
      "touch-action:none;box-shadow:0 -1px 8px rgba(0,0,0,.18);transition:opacity .15s}" +
    ".vkb-faded{opacity:.12}" +
    ".vkb-body{display:flex;flex-direction:column;gap:5px;box-sizing:border-box;" +
      "height:calc(5 * clamp(46px,8vh,72px) + 25px)}" +   /* 3×1 + 2×0.6 + 1×0.8 排 */
    ".vkb-row{display:flex;gap:5px;flex:1;min-height:0}" +
    ".vkb-thin{flex:0.6}" +   /* 第0/A 排：60% 高 */
    ".vkb-low{flex:0.8}" +   /* 功能排：80% 高 */
    ".vkb-key{flex:1;background:#fdfdfd;border-radius:6px;box-shadow:0 1px 0 rgba(0,0,0,.35);" +
      "display:flex;align-items:center;justify-content:center;position:relative;" +
      "color:#111;font-size:20px;cursor:pointer;min-width:0;overflow:hidden}" +   /* 主字母 */
    ".vkb-key:active{background:#aab2bd}" +
    ".vkb-fcell{flex:none;width:clamp(30px,7.5%,52px);background:#b9c0ca;font-size:11px}" +
    ".vkb-char{font-size:14px}" +
    ".vkb-wide{flex:1.5}" +
    ".vkb-space{flex:4.2}" +
    ".vkb-stagger{flex:none;height:1px}" +
    ".vkb-armed{background:#4a90d9;color:#fff}" +
    ".vkb-armed .vkb-sub{color:#dce9f7}" +
    ".vkb-sub{position:absolute;top:1px;right:3px;font-size:8px;color:#777}" +
    ".vkb-fin{position:absolute;right:2px;bottom:1px;font-size:9.5px;line-height:1.15;" +
      "color:#6dbf73;text-align:right;white-space:pre}" +
    ".vkb-sp{position:absolute;left:2px;top:1px;font-size:8.5px;color:#9aa}" +
    ".vkb-dot{font-size:16px}" +
    ".vkb-loupe{position:fixed;z-index:10002;width:180px;height:84px;border-radius:12px;" +
      "background:#fff;border:1px solid #bbb;box-shadow:0 4px 18px rgba(0,0,0,.25);" +
      "overflow:hidden;pointer-events:none;display:none}" +
    ".vkb-loupe-inner{position:absolute;overflow:hidden;white-space:pre-wrap;" +
      "word-wrap:break-word;overflow-wrap:break-word;color:#111;background:#fff}" +
    ".vkb-loupe-caret{position:absolute;width:2px;background:#4a90d9}";
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
  const letterKeys = [];   // [{el, code}] 字母三排
  let kBackq = null, kShift = null, kCtrl = null, kLang = null;

  function tableEntry(code) {
    const t = ime.getLayoutTable();
    const qw = global.ImeEditor && global.ImeEditor.LAYOUTS
      ? global.ImeEditor.LAYOUTS.qwerty : null;
    return (t && t[code]) || (qw && qw[code]) || null;
  }

  function mkKey(parent, cls, label) {
    const el = document.createElement("div");
    el.className = "vkb-key" + (cls ? " " + cls : "");
    if (label != null) {
      const cap = document.createElement("span");
      cap.className = "vkb-cap";
      cap.textContent = label;
      el.appendChild(cap);
    }
    parent.appendChild(el);
    return el;
  }
  function mkRow() {
    const r = document.createElement("div");
    r.className = "vkb-row";
    body.appendChild(r);
    return r;
  }
  function mkStagger(parent, i) {
    if (STAGGER[i] === "0%") return;   // 零错行不放量具（否则 flex gap 白吃 5px）
    const s = document.createElement("div");
    s.className = "vkb-stagger";
    s.style.width = STAGGER[i];
    parent.appendChild(s);
  }

  function refreshLabels() {
    const finals = cfg.getShuangpinFinals ? cfg.getShuangpinFinals() : null;
    for (const k of letterKeys) {
      const en = tableEntry(k.code);
      const ch = en ? (shiftArm ? en[1] : en[0]) : "";
      k.el.textContent = "";
      const cap = document.createElement("span");
      cap.className = "vkb-cap";
      cap.textContent = ch;
      k.el.appendChild(cap);
      const base = en && /^[a-z]$/.test(en[0]) ? en[0] : null;   // 韵母跟字符走
      if (base && finals) {
        const f = finals[base];
        if (f && f.length) {
          const s = document.createElement("span");
          s.className = "vkb-fin";
          s.textContent = f.join("\n");
          k.el.appendChild(s);
        }
        if (SP_HINT[base]) {
          const s2 = document.createElement("span");
          s2.className = "vkb-sp";
          s2.textContent = SP_HINT[base];
          k.el.appendChild(s2);
        }
      }
    }
    if (kBackq) {   // 唯一双标签键：`~
      const en = tableEntry("Backquote");
      kBackq.textContent = "";
      if (en) {
        const big = document.createElement("span");
        big.className = "vkb-cap";
        big.textContent = shiftArm ? en[1] : en[0];
        kBackq.appendChild(big);
        const sub = document.createElement("span");
        sub.className = "vkb-sub";
        sub.textContent = shiftArm ? en[0] : en[1];
        kBackq.appendChild(sub);
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
  function fireCode(code) {   // 布局表键位（字母、`~）：吃 sticky 修饰
    const en = tableEntry(code);
    if (!en) return;
    ime.sendKey({
      key: shiftArm ? en[1] : en[0],
      code, shiftKey: shiftArm, ctrlKey: ctrlArm,
    });
    afterFire();
  }
  function fireChar(ch) {   // 单字符直按（数字/符号排）：无视 sticky 武装，但会解除武装
    ime.sendKey({ key: ch, code: "", shiftKey: false, ctrlKey: ctrlArm });
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

  // 第0排：▽ + shifted 数字符号 + 补充符号（60% 高）
  const r0 = mkRow();
  r0.classList.add("vkb-thin");
  const kClose = mkKey(r0, "vkb-fcell", "▽");
  kClose.title = "收起键盘";
  pd(kClose, () => closeKb());
  for (const ch of SYM_ROW0) {
    const k = mkKey(r0, "vkb-char", ch);
    pd(k, () => fireChar(ch));
  }
  // 第A排：`~ + 数字 + 补充符号（60% 高）
  const rA = mkRow();
  rA.classList.add("vkb-thin");
  kBackq = mkKey(rA, "vkb-fcell");
  pd(kBackq, () => fireCode("Backquote"));
  for (const ch of SYM_ROWA) {
    const k = mkKey(rA, "vkb-char", ch);
    pd(k, () => fireChar(ch));
  }
  // 字母三排：左功能格 + 错行 + 字母（+ ⌫/⏎）
  const letterRows = [ROW1, ROW2, ROW3];
  let repTimer = null;   // ⌫ 长按重复定时器（destroy 时清理）
  for (let i = 0; i < 3; i++) {
    const r = mkRow();
    if (i === 0) {
      const kTab = mkKey(r, "vkb-fcell", "Tab");
      pd(kTab, () => fireNamed("Tab", "Tab"));
    } else if (i === 1) {
      const kEsc = mkKey(r, "vkb-fcell", "ESC");
      pd(kEsc, () => fireNamed("Escape", "Escape"));
    } else {
      kShift = mkKey(r, "vkb-fcell", "⇧");
      pd(kShift, () => { shiftArm = !shiftArm; refreshLabels(); });
    }
    mkStagger(r, i);   // 错行：字母区整体右错
    for (const code of letterRows[i]) {
      const k = mkKey(r, "");
      letterKeys.push({ el: k, code });
      pd(k, () => fireCode(code));
    }
    if (i === 0) {   // ⌫ 长按自动重复
      const kBksp = mkKey(r, "vkb-fcell vkb-wide", "⌫");
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
    }
    if (i === 2) {
      const kEnter = mkKey(r, "vkb-fcell vkb-wide", "⏎");
      pd(kEnter, () => fireNamed("Enter", "Enter"));
    }
  }
  // 第4排（功能行，80% 高）：Ctrl + 中/EN + 👆 + 空格 + 方向键（←/→ 长按 = Home/End）
  const r4 = mkRow();
  r4.classList.add("vkb-low");
  kCtrl = mkKey(r4, "vkb-fcell", "Ctrl");
  pd(kCtrl, () => { ctrlArm = !ctrlArm; refreshLabels(); });
  kLang = mkKey(r4, "vkb-fcell");
  pd(kLang, fireLang);
  const kDot = mkKey(r4, "vkb-fcell vkb-dot", "👆");
  kDot.title = "长按：放大镜拖动光标";
  const kSpace = mkKey(r4, "vkb-space");
  pd(kSpace, () => fireNamed(" ", "Space"));
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
  const kLeft = mkKey(r4, "vkb-fcell", "←");
  bindArrowLP(kLeft, "ArrowLeft", "Home");
  const kUp = mkKey(r4, "vkb-fcell", "↑");
  pd(kUp, () => fireNamed("ArrowUp"));
  const kDown = mkKey(r4, "vkb-fcell", "↓");
  pd(kDown, () => fireNamed("ArrowDown"));
  const kRight = mkKey(r4, "vkb-fcell", "→");
  bindArrowLP(kRight, "ArrowRight", "End");

  // ---------- 小红点：长按放大镜跟踪光标 ----------
  // 相对式触控板模型：手指拖动距离 1:1 映射成光标在文本里的像素移动
  // （横向按字、纵向按行，每越过一个步长发一个虚拟方向键，全套管线语义复用）；
  // 放大镜浮在文本光标附近并随光标走，键盘整体淡出。组词中不响应。
  const LOUPE_ZOOM = 1.75;
  let loupe = null, loupeInner = null, loupeCaret = null;
  let dotTimer = null, dotActive = false, dotPid = null;
  let dotStartX = 0, dotStartY = 0, dotLastX = 0, dotLastY = 0;
  let dotAccX = 0, dotAccY = 0, dotStepW = 8, dotStepH = 24;

  function buildLoupe() {
    loupe = document.createElement("div");
    loupe.className = "vkb-loupe";
    loupeInner = document.createElement("div");
    loupeInner.className = "vkb-loupe-inner";
    loupeCaret = document.createElement("div");
    loupeCaret.className = "vkb-loupe-caret";
    loupe.appendChild(loupeInner);
    document.body.appendChild(loupe);
  }
  buildLoupe();

  // 内容 = textarea 的 backdrop 镜像（含搜索高亮 <mark>，默认黑字黄底可见）
  function fillLoupeContent() {
    const cs = getComputedStyle(textarea);
    for (const p of ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing",
                     "paddingTop", "paddingRight", "paddingBottom", "paddingLeft",
                     "tabSize", "boxSizing"]) {
      loupeInner.style[p] = cs[p];
    }
    loupeInner.style.width = textarea.clientWidth + "px";
    loupeInner.style.height = textarea.clientHeight + "px";
    const bd = textarea.previousElementSibling;   // ime 的 backdrop（taWrap 内在 textarea 前）
    if (bd && bd.classList && bd.classList.contains("ime-backdrop")) {
      loupeInner.innerHTML = bd.innerHTML;
    } else {
      loupeInner.textContent = textarea.value;
    }
    loupeInner.appendChild(loupeCaret);   // innerHTML 后重挂光标标记
  }

  // inner 与屏幕文本 1:1 对齐，再以光标为 transform 原点缩放平移，让光标落到放大镜中心
  function updateLoupe() {
    const px = ime.getCaretPixel();         // 光标视口坐标 + width + lineHeight
    const taRect = textarea.getBoundingClientRect();
    const vw = window.innerWidth;
    const W = Math.min(180, vw - 16), H = 84;
    loupe.style.width = W + "px";
    loupe.style.height = H + "px";
    // 放大镜浮在光标上方；快到屏幕上沿则翻到光标下方
    const Lx = Math.min(Math.max(px.x - W / 2, 8), vw - W - 8);
    let Ly = px.y - H - 24;
    if (Ly < 8) Ly = px.y + px.lineHeight + 24;
    loupe.style.left = Lx + "px";
    loupe.style.top = Ly + "px";
    const il = taRect.left - Lx, it = taRect.top - Ly;   // inner 在放大镜内的落点
    loupeInner.style.left = il + "px";
    loupeInner.style.top = it + "px";
    const cl = px.x - taRect.left, ct = px.y - taRect.top;   // 光标在 inner 内的坐标
    loupeInner.style.transformOrigin = cl + "px " + ct + "px";
    loupeInner.style.transform =
      "translate(" + (W / 2 - il - cl) + "px," + (H / 2 - it - ct) + "px) scale(" + LOUPE_ZOOM + ")";
    loupeInner.scrollTop = textarea.scrollTop;   // 与文本区滚动同步
    loupeInner.scrollLeft = textarea.scrollLeft;
    loupeCaret.style.left = cl + "px";
    loupeCaret.style.top = ct + "px";
    loupeCaret.style.width = (2 / LOUPE_ZOOM) + "px";   // 缩放后视觉 ~2px
    loupeCaret.style.height = px.lineHeight + "px";
  }

  function activateDot() {
    if (ime.getMode().composing) return;      // 组词中不响应（组词光标用方向键）
    dotActive = true;
    if (navigator.vibrate) navigator.vibrate(10);
    // 步长：激活时量一次（等宽字体下近似恒定；混排有轻微漂移，v1 接受）
    const v = textarea.value;
    const ch = v[textarea.selectionStart] || "0";
    const px = ime.getCaretPixel(ch);
    dotStepW = Math.max(px.width, 4);
    dotStepH = px.lineHeight;
    panel.classList.add("vkb-faded");
    fillLoupeContent();
    loupe.style.display = "block";
    updateLoupe();
  }

  kDot.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    dotPid = e.pointerId;
    dotStartX = dotLastX = e.clientX;
    dotStartY = dotLastY = e.clientY;
    dotAccX = dotAccY = 0;
    dotActive = false;
    kDot.setPointerCapture(e.pointerId);
    dotTimer = setTimeout(activateDot, 350);
  });
  kDot.addEventListener("pointermove", (e) => {
    if (e.pointerId !== dotPid) return;
    const dx = e.clientX - dotLastX, dy = e.clientY - dotLastY;
    dotLastX = e.clientX; dotLastY = e.clientY;
    if (!dotActive) {
      if (Math.abs(e.clientX - dotStartX) + Math.abs(e.clientY - dotStartY) > 12) {
        clearTimeout(dotTimer);   // 提前滑走：取消长按
      }
      return;
    }
    dotAccX += dx; dotAccY += dy;
    let moved = false, guard = 0;
    while (guard++ < 60) {
      if (dotAccX >= dotStepW) { dotAccX -= dotStepW; ime.sendKey({ key: "ArrowRight" }); moved = true; }
      else if (dotAccX <= -dotStepW) { dotAccX += dotStepW; ime.sendKey({ key: "ArrowLeft" }); moved = true; }
      else if (dotAccY >= dotStepH) { dotAccY -= dotStepH; ime.sendKey({ key: "ArrowDown" }); moved = true; }
      else if (dotAccY <= -dotStepH) { dotAccY += dotStepH; ime.sendKey({ key: "ArrowUp" }); moved = true; }
      else break;
    }
    if (moved) updateLoupe();
  });
  function endDot(e) {
    if (e.pointerId !== dotPid) return;
    clearTimeout(dotTimer);
    if (dotActive) {
      dotActive = false;
      panel.classList.remove("vkb-faded");
      loupe.style.display = "none";
    }
    dotPid = null;
  }
  kDot.addEventListener("pointerup", endDot);
  kDot.addEventListener("pointercancel", endDot);

  // ---------- 开合 ----------
  function persist() {
    try { localStorage.setItem(LS_KEY, open ? "1" : "0"); } catch (e) { /* ignore */ }
  }
  function applyVisibility() {
    handle.style.display = enabled && !open ? "flex" : "none";
    panel.style.display = enabled && open ? "block" : "none";   // block：body 才能撑满
  }
  // 键区与 textarea 等宽、左缘对齐（面板背景仍全宽）
  function syncBodyWidth() {
    const r = textarea.getBoundingClientRect();
    body.style.width = r.width + "px";
    body.style.marginLeft = r.left + "px";
  }
  const basePadBottom = parseFloat(getComputedStyle(textarea).paddingBottom) || 0;
  function applyTextarea() {
    if (destroyed) return;
    syncBodyWidth();
    if (cfg.padContent) {
      textarea.style.paddingBottom = (basePadBottom + panel.offsetHeight) + "px";
    }
    if (cfg.coarse) {
      textarea.readOnly = true;              // 屏蔽系统键盘（iOS/Android 都吃 readonly）
      ime.setOption({ touchCaret: true });   // readonly 下无原生光标，改自绘细光标
    }
    textarea.focus();
    window.dispatchEvent(new Event("resize"));   // 触发编辑器行号/高亮/光标刷新
    cfg.onToggle(true, panel.offsetHeight);
  }
  function unapplyTextarea() {
    textarea.style.paddingBottom = "";
    if (cfg.coarse) {
      textarea.readOnly = false;
      ime.setOption({ touchCaret: false });
    }
    window.dispatchEvent(new Event("resize"));
    cfg.onToggle(false, 0);
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
      syncBodyWidth();
      if (cfg.padContent) {
        textarea.style.paddingBottom = (basePadBottom + panel.offsetHeight) + "px";
      }
      cfg.onToggle(true, panel.offsetHeight);   // 键盘高随 vh 变，页面同步收底
    }
  }
  window.addEventListener("resize", onResize);

  refreshLabels();
  applyVisibility();

  // 物理 Shift 切中英 → 同步中/EN 键面（ime-editor 较旧无此接口时静默跳过）
  const offMode = typeof ime.onModeChange === "function"
    ? ime.onModeChange(() => refreshLang()) : null;

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
      if (offMode) offMode();
      if (enabled && open) unapplyTextarea();
      window.removeEventListener("resize", onResize);
      clearTimeout(dotTimer);
      clearTimeout(repTimer);
      handle.remove(); panel.remove(); style.remove();
      if (loupe) loupe.remove();
    },
  };
}

const VirtualKeyboard = { attach };
if (typeof module !== "undefined" && module.exports) module.exports = VirtualKeyboard;
global.VirtualKeyboard = VirtualKeyboard;
})(typeof window !== "undefined" ? window : globalThis);
