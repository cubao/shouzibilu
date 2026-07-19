/*
 * ime-editor.js — 手自笔录 IME 编辑器
 *
 * 接管一个 <textarea> 的全部键盘输入，提供：
 *   - 物理键（e.code + 布局表）/ 字母（e.key）双模式键盘监听
 *   - 中文输入法（组合串 + 组词光标 + 候选弹窗悬停光标）
 *   - 动态词（`,date` → eval / `,check` → ✅，纯映射）
 *   - 精简 vim（Normal / Insert / Search，operator × motion / text-object）
 *
 * 用法：
 *   const inst = ImeEditor.attach(textarea, {
 *     getEngine: () => engineAdapter,   // {query, segment, commit, learnWord, dumpUser}
 *     layout: "dvorak4tzx",             // qwerty | dvorak | dvorak4tzx | 自定义表
 *     keyMode: "physical",              // physical | letter
 *     vim: true,
 *     punctStyle: "zh",                 // zh | en | tw
 *     pagingKeys: ",.",                 // 上一页/下一页；null 仅 PgUp/PgDn
 *     mappings: {",check": "✅", ",date": "eval:return ..."},
 *     indent: "    ",                   // vim >> 缩进串 / Tab 插入串
 *     onMode: (mode, english) => {},    // mode: NORMAL | INSERT | SEARCH
 *   });
 *   inst.setOption({layout: ...});      // 运行时改配置
 *   inst.onModeChange((mode, english) => {});   // 订阅模式/中英变化，返回退订函数
 *   inst.destroy();
 *
 * 程序注入接口（virtual-keyboard.js 触屏虚拟键盘走这里）：
 *   inst.sendKey({key, code, shiftKey, ctrlKey})   // 与物理键盘同一管线；
 *                                                    // 未 handled 的键模拟浏览器默认行为
 *   inst.sendKeyUp({key: "Shift"})                 // keyup（Shift 单按切中英依赖它）
 *   inst.getLayoutTable()                          // 当前布局表（code -> [unshifted, shifted]）
 *   inst.getCaretPixel()                           // 光标像素位置（相对视口）
 *   cfg.touchCaret: true                           // INSERT 也自绘细光标（触屏 readonly 下用）
 *
 * 边界声明：OS 级中文输入法激活时按键会被吞，页面无法压制；
 * 请把 OS 输入源切到英文。physical 模式屏蔽的是键盘布局，不是 OS 输入法。
 */
(function (global) {
"use strict";

// ============================== 布局表 ==============================
// e.code -> [unshifted, shifted]
function rowsToLayout(rows) {
  const t = {};
  const put = (codes, chars, shifted) => {
    for (let i = 0; i < codes.length; i++) t[codes[i]] = [chars[i], shifted[i]];
  };
  put("KeyQ KeyW KeyE KeyR KeyT KeyY KeyU KeyI KeyO KeyP".split(" "), rows[0], rows[1]);
  put("KeyA KeyS KeyD KeyF KeyG KeyH KeyJ KeyK KeyL".split(" "), rows[2], rows[3]);
  put("KeyZ KeyX KeyC KeyV KeyB KeyN KeyM".split(" "), rows[4], rows[5]);
  // 数字行与符号键：三张布局一致（QWERTY 原位）
  t.Backquote = ["`", "~"];
  t.Digit1 = ["1", "!"]; t.Digit2 = ["2", "@"]; t.Digit3 = ["3", "#"];
  t.Digit4 = ["4", "$"]; t.Digit5 = ["5", "%"]; t.Digit6 = ["6", "^"];
  t.Digit7 = ["7", "&"]; t.Digit8 = ["8", "*"]; t.Digit9 = ["9", "("];
  t.Digit0 = ["0", ")"];
  t.Minus = ["-", "_"]; t.Equal = ["=", "+"];
  t.BracketLeft = ["[", "{"]; t.BracketRight = ["]", "}"];
  t.Backslash = ["\\", "|"];
  t.Space = [" ", " "];
  return t;
}

const LAYOUTS = {
  qwerty: rowsToLayout([
    "qwertyuiop", "QWERTYUIOP",
    "asdfghjkl", "ASDFGHJKL",
    "zxcvbnm", "ZXCVBNM",
  ]) ,
  dvorak: rowsToLayout([
    "',.pyfgcrl", "\"<>PYFGCRL",
    "aoeuidhtns", "AOEUIDHTNS",
    ";qjkxbmwvz", ":QJKXBMWVZ",
  ]),
  // tangzhixiong 的 dvorak 变体（见 shuangpin-heatmap/shuangpin_heatmap.py）
  dvorak4tzx: rowsToLayout([
    ";,.kyfgclz", ":<>KYFGCLZ",
    "aoeiudrtsn", "AOEIUDRTSN",
    "pqjhxbmwv", "PQJHXBMWV",
  ]),
};
// 标点键三张布局不同（分号/引号/逗号/句号/斜杠位置都动过），单独补。
function setPunct(t, semi, quote, comma, period, slash) {
  t.Semicolon = semi; t.Quote = quote; t.Comma = comma;
  t.Period = period; t.Slash = slash;
}
setPunct(LAYOUTS.qwerty, [";", ":"], ["'", "\""], [",", "<"], [".", ">"], ["/", "?"]);
setPunct(LAYOUTS.dvorak, ["s", "S"], ["-", "_"], ["w", "W"], ["v", "V"], ["z", "Z"]);
setPunct(LAYOUTS.dvorak4tzx, ["n", "N"], ["'", "\""], ["w", "W"], ["v", "V"], ["/", "?"]);
// 注：dvorak 的 Minus 在 Quote 位、Quote 在 Slash 上方 Row1；
// 标准 dvorak 完整标点：Quote 位是 '-' 简化处理如上（Semicolon='s' 等
// 已由字母行覆盖的键以字母为准，这里只补物理标点位的产出）。
// dvorak: 物理 Semicolon 产出 's'（字母），物理 Quote 在 Row1 之外——
// 标准 dvorak 把 '-' 放在物理 Quote 位，'/' 位产出 'z'。保持上表。

const DEFAULT_MAPPINGS = {
  ",check": "✅",
  ",cross": "❌",
  ",star": "⭐",
  ",warn": "⚠️",
  ",ok": "🆗",
  ",shrug": "¯\\_(ツ)_/¯",
  ",date": "eval:return new Date().toLocaleDateString('sv')",
  ",time": "eval:return new Date().toTimeString().slice(0,8)",
  ",datetime": "eval:return new Date().toLocaleDateString('sv') + ' ' + new Date().toTimeString().slice(0,8)",
  ",uuid": "eval:return crypto.randomUUID()",
};

// ============================== 标点表 ==============================
const PUNCT = {
  ",": [",", "，", "，"], ".": [".", "。", "。"],
  "<": ["<", "《", "《"], ">": [">", "》", "》"],
  "?": ["?", "？", "？"], "!": ["!", "！", "！"],
  ";": [";", "；", "；"], ":": [":", "：", "："],
  "(": ["(", "（", "（"], ")": [")", "）", "）"],
  "[": ["[", "【", "【"], "]": ["]", "】", "】"],
  "{": ["{", "｛", "｛"], "}": ["}", "｝", "｝"],
  "\\": ["\\", "、", "、"], "|": ["|", "｜", "｜"],
  "~": ["~", "～", "～"], "^": ["^", "……", "……"],
  "_": ["_", "——", "——"], "`": ["`", "·", "·"],
  "'": ["'", "Q", "Q"], "\"": ["\"", "q", "q"],
};
const QUOTES = {
  zh: { "'": ["‘", "’"], "\"": ["“", "”"] },
  tw: { "'": ["『", "』"], "\"": ["「", "」"] },
};

// ============================== 纯文本工具 ==============================
// 字符类别：w=word(字母数字下划线) c=CJK(每字一词) p=punct(连续成词) s=空白
function chClass(ch) {
  if (/[A-Za-z0-9_]/.test(ch)) return "w";
  if (/\s/.test(ch)) return "s";
  if (/[㐀-鿿豈-﫿]/.test(ch)) return "c";
  return "p";
}

function motionW(v, pos) {
  const n = v.length;
  let i = pos;
  if (i < n && chClass(v[i]) !== "s") {
    if (chClass(v[i]) === "c") i++;
    else { const c = chClass(v[i]); while (i < n && chClass(v[i]) === c) i++; }
  }
  while (i < n && chClass(v[i]) === "s") i++;
  return Math.min(i, n);
}
function motionB(v, pos) {
  let i = pos - 1;
  while (i > 0 && chClass(v[i]) === "s") i--;
  if (i < 0) return 0;
  const c = chClass(v[i]);
  if (c === "c") return i;
  while (i - 1 >= 0 && chClass(v[i - 1]) === c) i--;
  return i;
}
function motionE(v, pos) {
  const n = v.length;
  let i = pos + 1;
  while (i < n && chClass(v[i]) === "s") i++;
  if (i >= n) return Math.max(0, n - 1);
  const c = chClass(v[i]);
  if (c === "c") return i;
  while (i + 1 < n && chClass(v[i + 1]) === c) i++;
  return i;
}

function lineStart(v, pos) { return v.lastIndexOf("\n", pos - 1) + 1; }
function lineEnd(v, pos) { const i = v.indexOf("\n", pos); return i < 0 ? v.length : i; }
function lineRangeOf(v, pos) { return [lineStart(v, pos), lineEnd(v, pos)]; }
function lineIndexOf(v, pos) { let n = 0; for (let i = 0; i < pos; i++) if (v[i] === "\n") n++; return n; }
function lineStartByIndex(v, li) {
  if (li <= 0) return 0;
  let n = 0;
  for (let i = 0; i < v.length; i++) if (v[i] === "\n" && ++n === li) return i + 1;
  return v.length;
}
function lineCount(v) { let n = 1; for (const ch of v) if (ch === "\n") n++; return n; }
// 行内首个非空白
function firstNonBlank(v, pos) {
  const [s, e] = lineRangeOf(v, pos);
  let i = s;
  while (i < e && (v[i] === " " || v[i] === "\t")) i++;
  return i;
}

// f/F/t/T：行内找字符。dir=1 向前，-1 向后。till=true 停在字符前一位。
function findChar(v, pos, ch, dir, till) {
  const [s, e] = lineRangeOf(v, pos);
  let i = pos + dir;
  while (i >= s && i < e) {
    if (v[i] === ch) return till ? i - dir : i;
    i += dir;
  }
  return null;
}

// 配对表（含中文括号/引号）
const PAIR_OPEN = { "(": ")", "[": "]", "{": "}", "<": ">",
                    "（": "）", "【": "】", "《": "》", "「": "」",
                    "“": "”", "‘": "’" };
const PAIR_CLOSE = {};
for (const [o, c] of Object.entries(PAIR_OPEN)) PAIR_CLOSE[c] = o;
const SYM_QUOTES = ["'", "\"", "`"];   // 同字符引号（行内配对）

function isBracket(ch) { return ch in PAIR_OPEN || ch in PAIR_CLOSE; }

// %：括号/引号配对跳。光标不在可配对字符上时向行尾扫描。返回目标位置或 null。
function matchPair(v, pos) {
  const [ls, le] = lineRangeOf(v, pos);
  let i = pos;
  while (i < le && !isBracket(v[i]) && !SYM_QUOTES.includes(v[i])) i++;
  if (i >= le) return null;
  const ch = v[i];
  if (ch in PAIR_OPEN) {
    let depth = 1;
    for (let j = i + 1; j < v.length; j++) {
      if (v[j] === ch) depth++;
      else if (v[j] === PAIR_OPEN[ch] && --depth === 0) return j;
    }
    return null;
  }
  if (ch in PAIR_CLOSE) {
    const open = PAIR_CLOSE[ch];
    let depth = 1;
    for (let j = i - 1; j >= 0; j--) {
      if (v[j] === ch) depth++;
      else if (v[j] === open && --depth === 0) return j;
    }
    return null;
  }
  // 同字符引号：先找下一个，找不到找上一个
  const fwd = v.indexOf(ch, i + 1);
  if (fwd >= 0) return fwd;
  const bwd = v.lastIndexOf(ch, i - 1);
  return bwd >= 0 ? bwd : null;
}

// 引号 text object（行内）。返回 [s, e) 或 null。around=true 含引号本身。
function quoteObject(v, pos, q, around) {
  const [ls, le] = lineRangeOf(v, pos);
  if (q in PAIR_OPEN || q in PAIR_CLOSE) {  // 中文弯引号按括号逻辑
    const open = q in PAIR_OPEN ? q : PAIR_CLOSE[q];
    const close = PAIR_OPEN[open];
    return bracketObject(v, pos, open, close, around, ls, le);
  }
  const idx = [];
  for (let i = ls; i < le; i++) if (v[i] === q) idx.push(i);
  for (let k = 0; k + 1 < idx.length; k += 2) {
    if (idx[k] <= pos && pos <= idx[k + 1]) {
      return around ? [idx[k], idx[k + 1] + 1] : [idx[k] + 1, idx[k + 1]];
    }
  }
  return null;
}

// 括号 text object（可跨行）。scope 限定时（弯引号）只在 [scopeS, scopeE) 内找。
function bracketObject(v, pos, open, close, around, scopeS, scopeE) {
  const S = scopeS ?? 0, E = scopeE ?? v.length;
  let depth = 0, s = -1;
  for (let i = pos; i >= S; i--) {
    if (v[i] === close) depth++;
    else if (v[i] === open) { if (depth === 0) { s = i; break; } depth--; }
  }
  if (s < 0) return null;
  depth = 0;
  for (let i = s + 1; i < E; i++) {
    if (v[i] === open) depth++;
    else if (v[i] === close) {
      if (depth === 0) return around ? [s, i + 1] : [s + 1, i];
      depth--;
    }
  }
  return null;
}

// word text object：每汉字一词、ASCII 连续串一词、标点连续串一词。
function wordObject(v, pos, around) {
  const n = v.length;
  if (n === 0) return null;
  let c = chClass(v[pos]);
  if (c === "s") {  // 光标在空白上：aw 选空白段，iw 无效
    if (!around) return null;
    let s = pos, e = pos;
    while (s > 0 && chClass(v[s - 1]) === "s" && v[s - 1] !== "\n") s--;
    while (e < n && chClass(v[e]) === "s" && v[e] !== "\n") e++;
    return [s, e];
  }
  let s, e;
  if (c === "c") { s = pos; e = pos + 1; }
  else {
    s = pos; e = pos;
    while (s > 0 && chClass(v[s - 1]) === c) s--;
    while (e < n && chClass(v[e]) === c) e++;
  }
  if (around) {  // 带尾随空白；无尾随则带前导
    let e2 = e;
    while (e2 < n && chClass(v[e2]) === "s" && v[e2] !== "\n") e2++;
    if (e2 > e) e = e2;
    else { while (s > 0 && chClass(v[s - 1]) === "s" && v[s - 1] !== "\n") s--; }
  }
  return [s, e];
}

// ============================== 动态词求值 ==============================
function evalMapping(expr) {
  try {
    const fn = new Function(expr);
    return String(fn());
  } catch (e) {
    return "⚠️ " + e.message;
  }
}

// ============================== 编辑器主体 ==============================
function attach(textarea, opts) {
  const cfg = Object.assign({
    getEngine: () => null,
    layout: "qwerty",
    keyMode: "physical",
    vim: true,
    punctStyle: "zh",
    pagingKeys: ",.",
    mappings: DEFAULT_MAPPINGS,
    indent: "    ",
    touchCaret: false,          // true = INSERT 模式也自绘细光标（触屏虚拟键盘用）
    onMode: () => {},
  }, opts);

  // ---------- 状态 ----------
  let mode = "INSERT";            // NORMAL | INSERT | SEARCH
  let english = false;
  let composition = "";
  let caret = 0;                  // 组词光标（composition 内的下标）
  let segInfo = { boundaries: [0], path: [0] };
  let candidates = [];
  let page = 0;
  let session = null;             // 自造词学习会话
  let shiftLone = false;
  const quoteOpen = { "'": true, "\"": true };

  // vim 状态
  let pendingOp = null;           // 'd' | 'c' | 'y' | '>'
  let pendingIA = null;           // 'i' | 'a'（text object 修饰）
  let pendingG = false;           // gg
  let pendingFind = null;         // {op, key:'f'|'F'|'t'|'T'}
  let pendingR = false;           // r<char> 等待替换字符
  let register = { text: "", linewise: false };
  let undoStack = [];             // [{value, cursor}]
  let lastSearch = null;          // {pat, dir}
  let searchDir = 1;
  let searchBuf = "";
  let searchComp = "";            // 搜索框内的 IME 组合串
  let searchCands = [];            // 搜索框内的候选
  let searchStartPos = 0;          // 进入搜索时的光标（incsearch 基点 / Esc 还原点）
  let hlActive = false;            // 搜索接受后高亮保留中
  let pendingComma = null;         // 刚上屏的字面逗号 {pos, len}（仅逗号前是空白/换行/文首时武装）；下一键 [a-z] 则删逗号进动态词
  const modeListeners = [];        // onModeChange 订阅者（virtual-keyboard 同步中/EN 键面）
  function notifyMode() {
    cfg.onMode(mode, english);
    for (const f of modeListeners.slice()) f(mode, english);
  }
  let preferredCol = null;        // j/k 列记忆

  const PAGE_SIZE = 9;

  // ---------- DOM：候选弹窗 ----------
  const popup = document.createElement("div");
  popup.className = "ime-popup";
  popup.style.cssText =
    "position:fixed;display:none;z-index:9999;background:#fff;border:1px solid #ccc;" +
    "border-radius:6px;box-shadow:0 4px 16px rgba(0,0,0,.15);padding:6px 10px;" +
    "font:16px/1.7 -apple-system,'PingFang SC',sans-serif;max-width:70vw;";
  popup.addEventListener("pointerdown", (e) => e.preventDefault());  // 不抢焦点（触屏零延迟）
  document.body.appendChild(popup);

  // mirror div：计算 textarea 光标像素位置
  const mirror = document.createElement("div");
  mirror.style.cssText = "position:absolute;left:-9999px;top:0;white-space:pre-wrap;" +
    "word-wrap:break-word;overflow-wrap:break-word;visibility:hidden;";
  document.body.appendChild(mirror);

  // 结构：wrap(flex) > gutter(行号) + taWrap > backdrop(高亮层) + textarea
  const wrap = document.createElement("div");
  wrap.className = "ime-wrap";
  wrap.style.cssText = "display:flex;align-items:stretch;";
  const gutter = document.createElement("div");
  gutter.className = "ime-gutter";
  gutter.setAttribute("aria-hidden", "true");
  const taWrap = document.createElement("div");
  taWrap.style.cssText = "position:relative;flex:1;min-width:0;";
  const hadFocus = document.activeElement === textarea;
  textarea.parentNode.insertBefore(wrap, textarea);
  const backdrop = document.createElement("div");
  backdrop.className = "ime-backdrop";
  backdrop.setAttribute("aria-hidden", "true");
  taWrap.appendChild(backdrop);
  taWrap.appendChild(textarea);
  wrap.appendChild(gutter);
  wrap.appendChild(taWrap);
  if (hadFocus) textarea.focus();
  const taBg = getComputedStyle(textarea).backgroundColor;
  backdrop.style.cssText =
    "position:absolute;inset:0;overflow:hidden;white-space:pre-wrap;" +
    "word-wrap:break-word;overflow-wrap:break-word;color:transparent;" +
    "pointer-events:none;background:" +
    (taBg === "rgba(0, 0, 0, 0)" || taBg === "transparent" ? "#fff" : taBg) + ";";
  textarea.style.background = "transparent";
  textarea.style.position = "relative";

  // NORMAL 模式的 block 光标（difference 混合反色块：字符仍由 textarea 渲染，
  // 字号/字高零变化，只是背景/前景反相）
  const blockCaret = document.createElement("div");
  blockCaret.className = "ime-block-caret";
  blockCaret.style.cssText =
    "position:fixed;display:none;z-index:9998;pointer-events:none;background:#fff;" +
    "mix-blend-mode:difference;border-radius:2px;";
  document.body.appendChild(blockCaret);

  // gutter 行高测量容器（屏外）
  const gutterMirror = document.createElement("div");
  gutterMirror.style.cssText = "position:absolute;left:-9999px;top:0;visibility:hidden;" +
    "white-space:pre-wrap;word-wrap:break-word;overflow-wrap:break-word;box-sizing:border-box;";
  document.body.appendChild(gutterMirror);

  function caretPixel(markerCh) {
    const ta = textarea;
    const cs = getComputedStyle(ta);
    for (const p of ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing",
                     "paddingTop", "paddingRight", "paddingBottom", "paddingLeft",
                     "borderTopWidth", "borderRightWidth", "borderBottomWidth",
                     "borderLeftWidth", "width", "boxSizing", "tabSize"]) {
      mirror.style[p] = cs[p];
    }
    const upto = ta.value.slice(0, ta.selectionStart);
    mirror.textContent = upto;
    const marker = document.createElement("span");
    marker.textContent = markerCh || "​";  // 默认 zero-width
    mirror.appendChild(marker);
    const rect = ta.getBoundingClientRect();
    const lh = parseFloat(cs.lineHeight) || parseFloat(cs.fontSize) * 1.5;
    return {
      x: rect.left + marker.offsetLeft - ta.scrollLeft,
      y: rect.top + marker.offsetTop - ta.scrollTop,
      width: marker.offsetWidth,
      lineHeight: lh,
    };
  }

  // 搜索高亮：把 pat 的所有命中包 <mark>，当前光标处命中用 ime-hl-cur
  function refreshHighlights() {
    const pat = mode === "SEARCH" ? searchBuf
              : (hlActive && lastSearch ? lastSearch.pat : "");
    const v = val();
    const cs = getComputedStyle(textarea);
    for (const p of ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing",
                     "paddingTop", "paddingRight", "paddingBottom", "paddingLeft",
                     "borderTopWidth", "borderRightWidth", "borderBottomWidth",
                     "borderLeftWidth", "tabSize", "boxSizing"]) {
      backdrop.style[p] = cs[p];
    }
    backdrop.style.borderStyle = "solid";
    backdrop.style.borderColor = "transparent";
    if (!pat) { backdrop.textContent = v + "​"; return; }
    let html = "", pos = 0;
    const curP = cur();
    for (let idx = v.indexOf(pat, pos); idx >= 0; idx = v.indexOf(pat, pos)) {
      html += escapeHtml(v.slice(pos, idx)) +
              (idx === curP ? '<mark class="ime-hl-cur">' : "<mark>") +
              escapeHtml(v.slice(idx, idx + pat.length)) + "</mark>";
      pos = idx + pat.length;
    }
    backdrop.innerHTML = html + escapeHtml(v.slice(pos)) + "​";
    backdrop.scrollTop = textarea.scrollTop;
    backdrop.scrollLeft = textarea.scrollLeft;
  }

  // incsearch：从进入搜索时的光标处找当前 searchBuf 的命中并跳过去
  function incSearchJump() {
    if (!searchBuf) { setCur(searchStartPos); return; }
    const v = val();
    let idx = searchDir > 0
      ? v.indexOf(searchBuf, searchStartPos)
      : v.lastIndexOf(searchBuf, Math.max(0, searchStartPos - 1));
    if (idx < 0 && searchDir < 0) idx = v.lastIndexOf(searchBuf);
    if (idx >= 0) setCur(idx);
  }

  // 键盘驱动光标后，最小滚动让光标行可见（scrolloff=0）
  function ensureCursorVisible() {
    const ta = textarea;
    const cs = getComputedStyle(ta);
    for (const p of ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing",
                     "paddingTop", "paddingRight", "paddingBottom", "paddingLeft",
                     "borderTopWidth", "borderRightWidth", "borderBottomWidth",
                     "borderLeftWidth", "width", "boxSizing", "tabSize"]) {
      mirror.style[p] = cs[p];
    }
    mirror.textContent = ta.value.slice(0, ta.selectionStart);
    const marker = document.createElement("span");
    marker.textContent = "​";
    mirror.appendChild(marker);
    const y = marker.offsetTop;
    const padTop = parseFloat(cs.paddingTop) || 0;
    const padBottom = parseFloat(cs.paddingBottom) || 0;
    const lh = parseFloat(cs.lineHeight) || parseFloat(cs.fontSize) * 1.5;
    if (y - ta.scrollTop < padTop) ta.scrollTop = y - padTop;
    else if (y - ta.scrollTop + lh > ta.clientHeight - padBottom) {
      ta.scrollTop = y + lh - ta.clientHeight + padBottom;
    }
    // 键盘驱动滚动：同步高亮层与行号（不等 scroll 事件，免除时序差）
    backdrop.scrollTop = ta.scrollTop;
    backdrop.scrollLeft = ta.scrollLeft;
    gutter.scrollTop = ta.scrollTop;
  }

  // 行号 gutter：量出每个逻辑行的折行高度，行号对齐首视觉行
  function refreshGutter() {
    const ta = textarea;
    const cs = getComputedStyle(ta);
    for (const p of ["fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing",
                     "tabSize"]) {
      gutterMirror.style[p] = cs[p];
    }
    const padTop = parseFloat(cs.paddingTop) || 0;
    const padBottom = parseFloat(cs.paddingBottom) || 0;
    gutterMirror.style.width = ta.clientWidth + "px";
    gutterMirror.style.padding =
      `${padTop}px ${cs.paddingRight} ${padBottom}px ${cs.paddingLeft}`;
    const lines = ta.value.split("\n");
    gutterMirror.innerHTML = lines.map((l) =>
      "<div>" + (escapeHtml(l) || " ") + "</div>").join("");
    // getBoundingClientRect 取小数高度，避免 offsetHeight 取整累计误差
    const hs = [...gutterMirror.children].map((c) => c.getBoundingClientRect().height);
    gutter.innerHTML = lines.map((_, i) =>
      `<div style="height:${hs[i]}px;line-height:${cs.lineHeight}">${i + 1}</div>`
    ).join("");
    gutter.style.paddingTop = padTop + "px";
    gutter.style.paddingBottom = padBottom + "px";
    gutter.style.fontFamily = cs.fontFamily;
    gutter.style.fontSize = cs.fontSize;
    gutter.scrollTop = ta.scrollTop;
  }

  function updateBlockCaret() {
    const thin = mode === "INSERT" && cfg.touchCaret;   // 触屏 readonly 下无原生光标，自绘细线
    if ((mode !== "NORMAL" && !thin) || document.activeElement !== textarea) {
      blockCaret.style.display = "none";
      return;
    }
    const v = val(), p = cur();
    const ch = p < v.length && v[p] !== "\n" ? v[p] : " ";
    const px = caretPixel(ch);   // 只量宽度；字形由 textarea 自己画
    blockCaret.style.left = px.x + "px";
    blockCaret.style.top = px.y + "px";
    blockCaret.style.width = (thin ? 2 : Math.max(px.width, 4)) + "px";
    blockCaret.style.height = px.lineHeight + "px";
    blockCaret.style.borderRadius = thin ? "0" : "2px";
    blockCaret.style.display = "block";
  }

  function showPopup(html) {
    popup.innerHTML = html;
    popup.style.display = "block";
    const { x, y, lineHeight } = caretPixel();
    const pw = popup.offsetWidth, ph = popup.offsetHeight;
    let top = y + lineHeight + 2;
    if (top + ph > window.innerHeight - 4) top = y - ph - 2;       // 近底翻上
    let left = Math.min(x, window.innerWidth - pw - 8);
    popup.style.top = Math.max(4, top) + "px";
    popup.style.left = Math.max(4, left) + "px";
  }
  function hidePopup() { popup.style.display = "none"; }

  // ---------- 文本操作 ----------
  const val = () => textarea.value;
  const cur = () => textarea.selectionStart;
  function setCur(p) {
    p = Math.max(0, Math.min(p, val().length));
    textarea.selectionStart = textarea.selectionEnd = p;
  }
  function pushUndo() {
    undoStack.push({ value: val(), cursor: cur() });
    if (undoStack.length > 50) undoStack.shift();
  }
  function applyText(newValue, newCursor, undoable = true) {
    if (undoable) pushUndo();
    textarea.value = newValue;
    setCur(newCursor);
    lastValue = newValue;
    refreshHighlights();
    refreshGutter();
  }
  function insertAt(text, pos) {
    const v = val();
    applyText(v.slice(0, pos) + text + v.slice(pos), pos + text.length, false);
  }
  // 外部编辑（Cmd+V 粘贴/拖拽）检测：补 undo 快照
  let lastValue = textarea.value;
  textarea.addEventListener("input", () => {
    if (textarea.value !== lastValue) {
      pushUndo();
      lastValue = textarea.value;
    }
    refreshHighlights();
    refreshGutter();
  });

  // ---------- 键解析 ----------
  function resolveChar(e) {
    if (cfg.keyMode === "letter") {
      return e.key.length === 1 ? e.key : null;
    }
    const table = typeof cfg.layout === "string" ? LAYOUTS[cfg.layout] : cfg.layout;
    const entry = table && table[e.code];
    if (!entry) return e.key.length === 1 ? e.key : null;  // 未覆盖键 fallback e.key
    return e.shiftKey ? entry[1] : entry[0];
  }

  function setMode(m) {
    if (mode === "INSERT" && m !== "INSERT") leaveInsert();
    mode = m;
    notifyMode();
    if (m !== "INSERT") hidePopup();
    if (m === "INSERT") pushUndoOnce();
    // NORMAL / 触屏 touchCaret：隐藏原生光标，画自绘光标
    textarea.style.caretColor = (m === "NORMAL" || cfg.touchCaret) ? "transparent" : "";
    updateBlockCaret();
  }
  // 进入 Insert 推一次 undo 快照（整个 Insert 会话 = 一个 undo 单位）
  let insertSnapshotTaken = false;
  function pushUndoOnce() {
    if (!insertSnapshotTaken) { pushUndo(); insertSnapshotTaken = true; }
  }
  function leaveInsert() { insertSnapshotTaken = false; }

  // ---------- IME 引擎适配 ----------
  const engine = () => cfg.getEngine();
  function refreshSegment() {
    if (!composition) { segInfo = { boundaries: [0], path: [0] }; return; }
    const r = engine().segment(composition);
    segInfo = r.error ? { boundaries: [0, composition.length], path: [0, composition.length] } : r;
  }
  function refreshCandidates() {
    if (!composition) { candidates = []; page = 0; return; }
    if (composition[0] === ",") { buildDynamicCandidates(); return; }
    const prefix = composition.slice(0, caret);
    const r = engine().query(prefix);
    candidates = (r.candidates || []).slice(0, 50);
  }

  // ---------- 动态词 ----------
  let dynList = [];   // [{key, text}]
  function buildDynamicCandidates() {
    candidates = [];
    const kw = composition;   // 含前导逗号
    if (kw.length < 2) { dynList = []; candidates = []; return; }  // 裸 , 不进菜单
    const entries = Object.entries(cfg.mappings || {});
    const exact = [], prefix = [];
    for (const [k, v] of entries) {
      if (k === kw) exact.push([k, v]);
      else if (k.startsWith(kw)) prefix.push([k, v]);
    }
    prefix.sort((a, b) => a[0] < b[0] ? -1 : 1);
    dynList = exact.concat(prefix).slice(0, PAGE_SIZE).map(([k, v]) => {
      const s = String(v);
      return { key: k, text: s.startsWith("eval:") ? evalMapping(s.slice(5)) : s };
    });
  }

  // ---------- 学习会话 ----------
  function sessionAccum(text, segments) {
    if (!session) session = { keys: [], text: "", commits: 0 };
    if (segments) for (const s of segments) session.keys.push(s.key);
    session.text += text;
    session.commits++;
  }
  function finishSession() {
    if (session && session.commits >= 2 &&
        session.text.length >= 2 && session.text.length <= 8) {
      engine().learnWord(session.keys.join(" "), session.text);
    }
    session = null;
  }
  function abortSession() { session = null; }

  // ---------- 提交 ----------
  function commitComposition(text, consumed, segments) {
    if (segments && segments.length) engine().commit(segments);
    sessionAccum(text, segments);
    insertAt(text, cur());
    composition = composition.slice(consumed).replace(/^'+/, "");
    caret = composition.length;
    page = 0;
    if (!composition) finishSession();
    refreshSegment(); refreshCandidates(); render();
  }
  function commitRaw(toSearch = false) {
    const raw = composition;
    abortSession();
    composition = ""; caret = 0; page = 0;
    if (toSearch) searchBuf += raw;
    else insertAt(raw, cur());
    refreshSegment(); refreshCandidates(); render();
  }
  function commitDynamic(item) {
    abortSession();           // 动态词不参与学习
    insertAt(item.text, cur());
    composition = ""; caret = 0; page = 0;
    refreshSegment(); refreshCandidates(); render();
  }
  // 动态词上屏：精确 > 首选 > 字面量「，+已输字母」（EN 模式出半角逗号）
  function commitDynamicBest() {
    const exact = dynList.find((d) => d.key === composition);
    if (exact) { commitDynamic(exact); return; }
    if (dynList.length) { commitDynamic(dynList[0]); return; }
    const raw = composition;
    composition = ""; caret = 0;
    insertAt((english ? "," : mapPunct(",")) + raw.slice(1), cur());
    refreshSegment(); refreshCandidates(); render();
  }

  // ---------- preedit 显示 ----------
  function preeditHTML() {
    const p = segInfo.path.length >= 2 ? segInfo.path : [0, composition.length];
    let out = "";
    for (let i = 0; i + 1 < p.length; i++) {
      const seg = composition.slice(p[i], p[i + 1]);
      // 段内含光标：插光标标记
      if (p[i] <= caret && caret <= p[i + 1]) {
        out += escapeHtml(composition.slice(p[i], caret)) +
               '<span class="ime-caret"></span>' +
               escapeHtml(composition.slice(caret, p[i + 1]));
      } else {
        out += escapeHtml(seg);
      }
      if (i + 2 < p.length) out += " ";
    }
    if (caret >= composition.length &&
        (p.length < 2 || caret > p[p.length - 2])) {
      // 光标在末尾（上一循环未画）
      if (!out.includes("ime-caret")) out += '<span class="ime-caret"></span>';
    }
    return out.replace(/ ' /g, "'").replace(/^ ' | ' $/g, "'");
  }
  function escapeHtml(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  // ---------- 渲染 ----------
  function render() {
    if (mode === "SEARCH") return renderSearch();
    if (mode !== "INSERT" || !composition) { hidePopup(); return; }
    let html = '<div class="ime-preedit" style="color:#4a90d9">' + preeditHTML() + "</div>";
    if (composition[0] === ",") {
      html += '<div class="ime-cands">' + dynList.map((c, i) =>
        `<span class="ime-cand" data-dyn="${i}" style="cursor:pointer;margin-right:12px">` +
        `<span style="color:#999;font-size:12px">${i + 1}</span> ${escapeHtml(c.text)}` +
        `<span style="color:#bbb;font-size:11px"> ${escapeHtml(c.key)}</span></span>`).join("") + "</div>";
    } else if (candidates.length) {
      const total = Math.max(1, Math.ceil(candidates.length / PAGE_SIZE));
      if (page >= total) page = total - 1;
      const slice = candidates.slice(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
      html += '<div class="ime-cands">' + slice.map((c, i) =>
        `<span class="ime-cand" data-idx="${i}" style="cursor:pointer;margin-right:12px">` +
        `<span style="color:#999;font-size:12px">${i + 1}</span> ${escapeHtml(c.text)}</span>`).join("") +
        (candidates.length > PAGE_SIZE
          ? `<span style="color:#999;font-size:12px">${page + 1}/${total}页</span>` : "") +
        "</div>";
    }
    showPopup(html);
    popup.querySelectorAll(".ime-cand").forEach((el) => {
      el.addEventListener("pointerdown", (ev) => {
        ev.preventDefault();
        if (el.dataset.dyn != null) commitDynamic(dynList[+el.dataset.dyn]);
        else {
          const c = candidates[page * PAGE_SIZE + (+el.dataset.idx)];
          if (c) commitComposition(c.text, c.consumed, c.segments);
        }
      });
    });
  }

  function renderSearch() {
    let html = `<div>${searchDir > 0 ? "/" : "?"}${escapeHtml(searchBuf)}` +
               '<span class="ime-caret"></span>' + escapeHtml(searchComp) + "</div>";
    searchCands = [];
    if (searchComp && !english) {
      const r = engine().query(searchComp);
      searchCands = (r.candidates || []).slice(0, PAGE_SIZE);
    }
    if (searchCands.length) {
      html += '<div>' + searchCands.map((c, i) =>
        `<span class="ime-cand" data-sc="${i}" style="cursor:pointer;margin-right:12px">` +
        `<span style="color:#999;font-size:12px">${i + 1}</span> ${escapeHtml(c.text)}</span>`
      ).join("") + "</div>";
    }
    showPopup(html);
    popup.querySelectorAll(".ime-cand[data-sc]").forEach((el) => {
      el.addEventListener("pointerdown", (ev) => {
        ev.preventDefault();
        const c = searchCands[+el.dataset.sc];
        if (c) { searchBuf += c.text; searchComp = ""; renderSearch(); }
      });
    });
    incSearchJump();
    refreshHighlights();
  }

  // ---------- 标点 ----------
  function mapPunct(key) {
    const style = cfg.punctStyle;
    if (style === "en") return key;
    const entry = PUNCT[key];
    if (!entry) return key;
    const mapped = entry[style === "zh" ? 1 : 2];
    if (mapped === "Q" || mapped === "q") {
      const pair = QUOTES[style][key];
      const out = quoteOpen[key] ? pair[0] : pair[1];
      quoteOpen[key] = !quoteOpen[key];
      return out;
    }
    return mapped;
  }

  // ---------- Insert 模式按键 ----------
  function pagingStep(key) {
    if (key === "PageDown") return 1;
    if (key === "PageUp") return -1;
    if (!cfg.pagingKeys) return 0;
    if (key === cfg.pagingKeys[0]) return -1;
    if (key === cfg.pagingKeys[1]) return 1;
    return 0;
  }

  // 返回 true = 已处理（preventDefault）
  function onInsertKey(e) {
    const key = e.key;

    if (key === "Shift") { shiftLone = true; return false; }
    shiftLone = false;

    // 逗号一次性标记：非「紧跟小写字母」的任何真实按键都作废（修饰键除外）
    if (pendingComma && !["Shift", "Control", "Alt", "Meta"].includes(key)) {
      const c0 = resolveChar(e);
      if (!c0 || !/^[a-z]$/.test(c0)) pendingComma = null;
    }

    if (key === "Escape") {
      if (composition) {
        abortSession();
        composition = ""; caret = 0; page = 0;
        refreshSegment(); refreshCandidates(); render();
      } else if (cfg.vim) {
        leaveInsert(); setMode("NORMAL");
      }
      return true;
    }
    const ch = resolveChar(e);
    // EN 模式：除动态词相关（, 或紧跟逗号的字母）外全透传（布局解析对 en/cn 一致）
    if (english && !composition &&
        ch !== "," && !(pendingComma && /^[a-z]$/.test(ch || ""))) return false;

    // --- 无缓冲：少量特殊键，其余透传/字面量 ---
    if (!composition) {
      if (key === "Backspace" || key === "Delete" || key.startsWith("Arrow") ||
          key === "Home" || key === "End") { pendingComma = null; return false; }
      if (key === "Enter") { pendingComma = null; insertAt("\n", cur()); return true; }
      if (key === "Tab") { pendingComma = null; insertAt(cfg.indent, cur()); return true; }
      if (ch == null) { pendingComma = null; return false; }
      if (ch === ",") {
        // 逗号：立即上屏字面量（英态半角），不进模式；下一键 [a-z] 才转动态词。
        // 武装前提：逗号前是空格/tab/换行（或文首）——否则只是普通逗号，
        // 避免中文「，」后接拼音被误吞进动态词
        const p0 = cur();
        const c0 = p0 > 0 ? val()[p0 - 1] : "\n";
        const lit = english ? "," : mapPunct(",");
        insertAt(lit, p0);
        pendingComma = (c0 === " " || c0 === "\t" || c0 === "\n")
          ? { pos: cur(), len: lit.length } : null;
        return true;
      }
      if (/^[a-z]$/.test(ch)) {
        if (pendingComma && cur() === pendingComma.pos) {
          // 紧跟逗号的字母：删掉刚上屏的逗号，转入动态词模式
          const v = val();
          applyText(v.slice(0, cur() - pendingComma.len) + v.slice(cur()),
                    cur() - pendingComma.len, false);
          composition = "," + ch; caret = 2; page = 0;
        } else {
          composition = ch; caret = 1; page = 0;
        }
        pendingComma = null;
        refreshSegment(); refreshCandidates(); render();
        return true;
      }
      pendingComma = null;
      if (/^[A-Z]$/.test(ch)) { insertAt(ch, cur()); return true; }
      if (/^[0-9]$/.test(ch)) { insertAt(ch, cur()); return true; }
      if (ch.length === 1 && !/[a-zA-Z0-9]/.test(ch)) {
        insertAt(mapPunct(ch), cur());   // 无缓冲标点直接上屏
        return true;
      }
      return false;
    }

    // --- 有缓冲 ---
    if (key === "Backspace") {
      abortSession();
      if (caret > 0) {
        composition = composition.slice(0, caret - 1) + composition.slice(caret);
        caret--;
      }
      page = 0;
      refreshSegment(); refreshCandidates(); render();
      return true;
    }
    if (key === "Enter") {
      if (composition[0] === ",") { commitDynamicBest(); return true; }
      commitRaw();
      return true;
    }
    if (key === "Tab") return true;      // 输入中 Tab 无意义，吞掉防焦点逃逸
    if (key === "ArrowLeft" || key === "ArrowRight") {
      if (composition[0] === ",") return true;   // 动态词模式不光标
      const b = segInfo.boundaries;
      if (key === "ArrowLeft") {
        for (let i = b.length - 1; i >= 0; i--) if (b[i] < caret) { caret = b[i]; break; }
      } else {
        for (let i = 0; i < b.length; i++) if (b[i] > caret) { caret = b[i]; break; }
      }
      page = 0;
      refreshCandidates(); render();
      return true;
    }
    if (key === "Home" || key === "End") {
      if (composition[0] !== ",") {
        caret = key === "Home" ? segInfo.boundaries[0]
                               : segInfo.boundaries[segInfo.boundaries.length - 1];
        page = 0;
        refreshCandidates(); render();
      }
      return true;
    }

    const step = pagingStep(key);
    if (step && composition[0] !== ",") {
      const total = Math.max(1, Math.ceil(candidates.length / PAGE_SIZE));
      page = (page + step + total) % total;
      render();
      return true;
    }

    if (ch == null) return true;
    if (/^[a-z]$/.test(ch)) {
      composition = composition.slice(0, caret) + ch + composition.slice(caret);
      caret++;
      page = 0;
      refreshSegment(); refreshCandidates(); render();
      return true;
    }
    if (ch === "'" && composition[0] !== ",") {
      composition = composition.slice(0, caret) + ch + composition.slice(caret);
      caret++;
      refreshSegment(); refreshCandidates(); render();
      return true;
    }
    if (/^[A-Z]$/.test(ch) && composition[0] !== ",") {
      // Shift+字母：上屏首候选（无候选原样），再写大写字母
      if (candidates.length) commitComposition(candidates[0].text, candidates[0].consumed,
                                               candidates[0].segments);
      else commitRaw();
      insertAt(ch, cur());
      return true;
    }
    if (/^[1-9]$/.test(ch)) {
      const idx = page * PAGE_SIZE + parseInt(ch, 10) - 1;
      if (composition[0] === ",") {
        if (dynList[idx]) { commitDynamic(dynList[idx]); return true; }
        if (composition.length === 1) {   // 裸 , + 数字 = 字面逗号 + 数字
          commitDynamicBest();
          insertAt(ch, cur());
        }
        return true;
      }
      if (candidates[idx]) commitComposition(candidates[idx].text, candidates[idx].consumed,
                                             candidates[idx].segments);
      else { commitRaw(); insertAt(ch, cur()); }
      return true;
    }
    if (ch === " ") {
      if (composition[0] === ",") { commitDynamicBest(); return true; }
      if (candidates.length) commitComposition(candidates[0].text, candidates[0].consumed,
                                               candidates[0].segments);
      else commitRaw();
      return true;
    }
    if (composition[0] === ",") {
      // 逗号模式其余可打印键：上屏最佳（裸逗号出字面逗号）再出该键
      if (ch.length !== 1) return true;
      commitDynamicBest();
      insertAt(english ? ch : mapPunct(ch), cur());
      return true;
    }
    if (ch.length === 1 && !/[a-zA-Z0-9]/.test(ch)) {
      // 标点键：首选上屏再出标点
      if (candidates.length) commitComposition(candidates[0].text, candidates[0].consumed,
                                               candidates[0].segments);
      else commitRaw();
      insertAt(mapPunct(ch), cur());
      return true;
    }
    return true;
  }

  // ============================== vim ==============================
  function motionPos(key) {
    // 返回 {to, inclusive, linewise} 或 null
    const v = val(), p = cur();
    switch (key) {
      case "h": return { to: Math.max(0, p - 1) };
      case "l": return { to: Math.min(v.length, p + 1) };
      case "j": case "k": {
        const li = lineIndexOf(v, p);
        const t = key === "j" ? li + 1 : li - 1;
        if (t < 0 || t >= lineCount(v)) return { to: p, linewise: true };
        const col = preferredCol ?? (p - lineStart(v, p));
        preferredCol = col;
        const ts = lineStartByIndex(v, t);
        const te = lineEnd(v, ts);
        return { to: Math.min(ts + col, te), linewise: true };
      }
      case "0": return { to: lineStart(v, p) };
      case "^": return { to: firstNonBlank(v, p) };
      case "$": return { to: lineEnd(v, p) };
      case "w": return { to: motionW(v, p) };
      case "b": return { to: motionB(v, p) };
      case "e": return { to: motionE(v, p), inclusive: true };
      case "G": return { to: v.length ? v.length - 1 : 0, linewise: true };
      case "%": { const m = matchPair(v, p); return m == null ? null : { to: m, inclusive: true }; }
      default: return null;
    }
  }

  function rangeOf(op, m) {
    // operator × motion 的文本范围 [s, e)
    const p = cur();
    if (m.linewise) {
      const a = Math.min(p, m.to), b = Math.max(p, m.to);
      const s = lineStart(val(), a);
      let e = lineEnd(val(), b);
      if (e < val().length) e++;   // 含换行
      return [s, e, true];
    }
    const s = Math.min(p, m.to), e = Math.max(p, m.to) + (m.inclusive ? 1 : 0);
    return [s, e, false];
  }

  function doOperator(op, s, e, linewise) {
    const v = val();
    if (op === "y") {
      register = { text: v.slice(s, e), linewise };
      setCur(s);
      return;
    }
    if (op === ">") {
      // 对范围内每一行加缩进（空行跳过），光标落在首个非空白
      const lines = v.slice(s, e).split("\n");
      const out = lines.map((l, i) =>
        (i === lines.length - 1 && l === "") ? l : (l === "" ? l : cfg.indent + l));
      applyText(v.slice(0, s) + out.join("\n") + v.slice(e), s);
      setCur(firstNonBlank(val(), s));
      return;
    }
    // d / c
    register = { text: v.slice(s, e), linewise };
    if (op === "d") {
      applyText(v.slice(0, s) + v.slice(e), Math.min(s, Math.max(0, val().length - (e - s))));
      setCur(Math.min(s, val().length));
    } else {  // c：删后进 Insert（cc 保留行本身）
      if (linewise) {
        const ls = lineStart(v, s);
        const le = lineEnd(v, s);
        applyText(v.slice(0, ls) + v.slice(le), ls);
        setCur(ls);
      } else {
        applyText(v.slice(0, s) + v.slice(e), s);
      }
      setMode("INSERT");
    }
  }

  function doLinewise(op) {
    const v = val(), p = cur();
    const s = lineStart(v, p);
    let e = lineEnd(v, p);
    if (e < v.length) e++;
    if (op === "<") {  // << 反缩进，光标落在首个非空白
      const line = v.slice(s, lineEnd(v, p));
      let n = 0;
      while (n < cfg.indent.length && line[n] === cfg.indent[n]) n++;
      if (n > 0) applyText(v.slice(0, s) + line.slice(n) + v.slice(lineEnd(v, p)), s);
      setCur(firstNonBlank(val(), s));
      return;
    }
    if (op === "c") {  // cc：清空行内容（保留行本身）进 Insert
      register = { text: v.slice(s, e), linewise: true };
      const le = lineEnd(v, p);
      applyText(v.slice(0, s) + v.slice(le), s);
      setCur(s);
      setMode("INSERT");
      return;
    }
    doOperator(op, s, e, true);
  }

  function doPaste(after) {
    if (!register.text) return;
    const v = val(), p = cur();
    if (register.linewise) {
      const li = lineIndexOf(v, p) + (after ? 1 : 0);
      let at = lineStartByIndex(v, li);
      let text = register.text.replace(/\n$/, "");
      if (at >= v.length && v.length && !v.endsWith("\n")) {
        applyText(v + "\n" + text, at + 1);
      } else {
        applyText(v.slice(0, at) + text + "\n" + v.slice(at), at);
      }
    } else {
      const at = after ? Math.min(p + 1, v.length) : p;
      applyText(v.slice(0, at) + register.text + v.slice(at), at + register.text.length);
      if (after) setCur(Math.min(at + register.text.length, val().length));
    }
  }

  // 合并行：J = 下一行并到当前行（去前导空白、中间补一个空格）；gJ = 直接并不补
  function doJoin(withSpace) {
    const v = val(), p = cur();
    const le = lineEnd(v, p);
    if (le >= v.length) return;              // 已是末行
    const head = v.slice(0, le);
    const tail = v.slice(le + 1);
    if (withSpace) {
      const t = tail.replace(/^[ \t]+/, "");
      const sep = (head === "" || /[ \t]$/.test(head) || t === "") ? "" : " ";
      applyText(head + sep + t, le);
    } else {
      applyText(head + tail, le);
    }
  }

  function doSearch(pat, dir) {
    if (!pat) return;
    const v = val(), p = cur();
    let idx = -1;
    if (dir > 0) {
      idx = v.indexOf(pat, p + 1);
      if (idx < 0) idx = v.indexOf(pat);            // 绕回
    } else {
      idx = v.lastIndexOf(pat, p - 1);
      if (idx < 0) idx = v.lastIndexOf(pat);
    }
    if (idx >= 0) setCur(idx);
  }

  // Normal 模式按键。返回 true = 已处理。
  function onNormalKey(e) {
    const key = e.key;
    if (key === "Shift" || key === "Control" || key === "Alt" || key === "Meta") return false;

    // f/F/t/T 等待字符
    if (pendingFind) {
      const ch = resolveChar(e);
      const pf = pendingFind;
      pendingFind = null;
      if (ch == null || key === "Escape") return true;
      const dir = (pf.key === "f" || pf.key === "t") ? 1 : -1;
      const till = pf.key === "t" || pf.key === "T";
      const to = findChar(val(), cur(), ch, dir, till);
      if (to == null) return true;
      if (pf.op) {
        const [s, e2, lw] = rangeOf(pf.op, { to, inclusive: !till });
        doOperator(pf.op, s, e2, lw);
      } else setCur(to);
      return true;
    }

    if (key === "Escape") {
      pendingOp = null; pendingIA = null; pendingG = false; pendingR = false;
      if (hlActive) { hlActive = false; refreshHighlights(); }   // Esc = :noh
      return true;
    }

    // r<char>：替换光标处字符（Enter = 拆行），留在 NORMAL
    if (pendingR) {
      pendingR = false;
      const v = val(), p = cur();
      if (p >= v.length || v[p] === "\n") return true;   // 空行/EOF 拒绝
      let rep = null;
      if (key === "Enter") rep = "\n";
      else {
        const c = resolveChar(e);
        if (c != null && c.length === 1) rep = c;
      }
      if (rep != null) applyText(v.slice(0, p) + rep + v.slice(p + 1), p);
      return true;
    }

    // gg（可带 operator：dgg/ygg/cgg）——必须先于 pendingOp 判断
    if (pendingG) {
      pendingG = false;
      const ch = resolveChar(e);
      const op = pendingOp;
      pendingOp = null;
      if (ch === "g") {
        if (op) {   // 作用于「到文首」的行式范围
          const [s, e2, lw] = rangeOf(op, { to: 0, linewise: true });
          doOperator(op, s, e2, lw);
        } else setCur(0);
      } else if (ch === "J" && !op) {   // gJ：合并行（不补空格）
        doJoin(false);
      }
      return true;
    }

    // operator 待命中
    if (pendingOp) {
      const op = pendingOp;
      if (pendingIA) {
        // text object：op + i/a + 对象
        const ch = resolveChar(e);
        pendingOp = null;
        const around = pendingIA === "a";
        pendingIA = null;
        if (ch == null) return true;
        let r = null;
        if (ch === "w") r = wordObject(val(), cur(), around);
        else if (SYM_QUOTES.includes(ch) || ch in PAIR_OPEN || ch in PAIR_CLOSE ||
                 ch === "“" || ch === "”" || ch === "‘" || ch === "’") {
          r = ch in PAIR_OPEN || ch in PAIR_CLOSE
            ? bracketObject(val(), cur(),
                ch in PAIR_OPEN ? ch : PAIR_CLOSE[ch],
                ch in PAIR_OPEN ? PAIR_OPEN[ch] : ch, around)
            : quoteObject(val(), cur(), ch, around);
        }
        if (r) doOperator(op, r[0], r[1], false);
        return true;
      }
      if (key === "g") { pendingG = true; return true; }   // dgg/ygg/cgg：保持 pendingOp
      const ch = resolveChar(e);
      pendingOp = null;
      if (ch === op || (op === ">" && ch === ">")) { doLinewise(op); return true; }
      if (op === ">" && ch === "<") { doLinewise("<"); return true; }
      if (ch === "i" || ch === "a") { pendingOp = op; pendingIA = ch; return true; }
      if (ch === "g") { pendingG = true; pendingOp = op; return true; }
      if (ch === "f" || ch === "F" || ch === "t" || ch === "T") {
        pendingFind = { op, key: ch };
        return true;
      }
      const m = ch && ch.length === 1 ? motionPos(ch) : motionPos(key);
      if (m) {
        const [s, e2, lw] = rangeOf(op, m);
        doOperator(op, s, e2, lw);
      }
      return true;
    }

    // 无 pending
    if (key === "Enter") return true;             // 防误插换行
    if (key.startsWith("Arrow")) {
      const m = { ArrowLeft: "h", ArrowRight: "l", ArrowDown: "j", ArrowUp: "k" }[key];
      const mm = motionPos(m);
      if (mm) { setCur(mm.to); if (m !== "j" && m !== "k") preferredCol = null; }
      return true;
    }
    const ch = resolveChar(e);
    if (ch == null) return true;
    if (ch === "g") { pendingG = true; return true; }
    if (ch === "f" || ch === "F" || ch === "t" || ch === "T") {
      pendingFind = { op: null, key: ch };
      return true;
    }
    switch (ch) {
      case "h": case "l": case "0": case "^": case "$":
      case "w": case "b": case "e": case "G": case "%": {
        const m = motionPos(ch);
        if (m) { setCur(m.to); preferredCol = null; }
        return true;
      }
      case "j": case "k": {
        const m = motionPos(ch);
        if (m) setCur(m.to);
        return true;
      }
      case "i": setMode("INSERT"); return true;
      case "a": setCur(Math.min(cur() + 1, val().length)); setMode("INSERT"); return true;
      case "A": setCur(lineEnd(val(), cur())); setMode("INSERT"); return true;
      case "I": setCur(firstNonBlank(val(), cur())); setMode("INSERT"); return true;
      case "r": pendingR = true; return true;
      case "o": {
        const le = lineEnd(val(), cur());
        applyText(val().slice(0, le) + "\n" + val().slice(le), le + 1);
        setMode("INSERT");
        return true;
      }
      case "O": {
        const ls = lineStart(val(), cur());
        applyText(val().slice(0, ls) + "\n" + val().slice(ls), ls);
        setMode("INSERT");
        return true;
      }
      case "x": {
        const v = val(), p = cur();
        if (p < v.length && v[p] !== "\n") {
          register = { text: v[p], linewise: false };
          applyText(v.slice(0, p) + v.slice(p + 1), p);
        }
        return true;
      }
      case "J": doJoin(true); return true;   // J：合并行（补一个空格）
      case "d": case "c": case "y": case ">": pendingOp = ch; return true;
      case "<": doLinewise("<"); return true;
      case "p": doPaste(true); return true;
      case "P": doPaste(false); return true;
      case "u": {
        const st = undoStack.pop();
        if (st) { textarea.value = st.value; setCur(st.cursor); lastValue = st.value; }
        return true;
      }
      case "/": case "?": {
        searchDir = ch === "/" ? 1 : -1;
        searchBuf = ""; searchComp = "";
        searchStartPos = cur();
        hlActive = false;
        setMode("SEARCH");
        renderSearch();
        return true;
      }
      case "n": if (lastSearch) { doSearch(lastSearch.pat, lastSearch.dir); refreshHighlights(); } return true;
      case "N": if (lastSearch) { doSearch(lastSearch.pat, -lastSearch.dir); refreshHighlights(); } return true;
      default: return true;   // Normal 模式吞掉一切可打印键
    }
  }

  // Search 模式按键（遵循当前 en/cn 状态：cn 下 IME 组词，en 下字面输入）
  function onSearchKey(e) {
    const key = e.key;
    if (key === "Shift") { shiftLone = true; return false; }
    shiftLone = false;
    if (key === "Escape") {
      if (searchComp) { searchComp = ""; renderSearch(); }
      else {   // 取消搜索：光标回起点、清高亮（先退模式再刷新，否则 pat 仍取 searchBuf）
        setCur(searchStartPos);
        hlActive = false;
        setMode(cfg.vim ? "NORMAL" : "INSERT");
        refreshHighlights();
      }
      return true;
    }
    if (key === "Enter") {
      if (searchComp) { searchBuf += searchComp; searchComp = ""; renderSearch(); }
      else {   // 接受搜索：光标已在 incsearch 命中处，高亮保留
        lastSearch = { pat: searchBuf, dir: searchDir };
        hlActive = true;
        refreshHighlights();
        setMode(cfg.vim ? "NORMAL" : "INSERT");
      }
      return true;
    }
    if (key === "Backspace") {
      if (searchComp) { searchComp = searchComp.slice(0, -1); renderSearch(); }
      else if (searchBuf) { searchBuf = searchBuf.slice(0, -1); renderSearch(); }
      else {
        setCur(searchStartPos);
        hlActive = false;
        setMode(cfg.vim ? "NORMAL" : "INSERT");
        refreshHighlights();
      }
      return true;
    }
    const ch = resolveChar(e);
    if (ch == null) return true;
    if (english) {   // EN：搜索串 = 字面文本
      searchBuf += ch;
      renderSearch();
      return true;
    }
    if (/^[a-z]$/.test(ch)) { searchComp += ch; renderSearch(); return true; }
    if (ch === " ") {
      if (searchComp) {
        searchBuf += searchCands.length ? searchCands[0].text : searchComp;
        searchComp = "";
      } else searchBuf += " ";
      renderSearch();
      return true;
    }
    if (/^[0-9]$/.test(ch)) {
      if (searchComp) {
        const c = searchCands[parseInt(ch, 10) - 1];
        if (c) { searchBuf += c.text; searchComp = ""; }
      } else searchBuf += ch;
      renderSearch();
      return true;
    }
    if (ch.length === 1) {   // 其余字符（含大写、标点）直接入搜索串
      if (searchComp) { searchBuf += searchComp; searchComp = ""; }
      searchBuf += ch;
      renderSearch();
      return true;
    }
    return true;
  }

  // ============================== 事件接管 ==============================
  function isEscAlias(e) {
    return (e.ctrlKey && !e.metaKey && !e.altKey &&
            (e.key === "[" || e.key === "c"));
  }

  function onKeydown(e) {
    if (isEscAlias(e)) {
      e.preventDefault();
      if (mode === "SEARCH") {
        if (searchComp) { searchComp = ""; renderSearch(); }
        else setMode(cfg.vim ? "NORMAL" : "INSERT");
      } else if (mode === "INSERT") {
        if (composition) {
          abortSession();
          composition = ""; caret = 0; page = 0;
          refreshSegment(); refreshCandidates(); render();
        } else if (cfg.vim) { leaveInsert(); setMode("NORMAL"); }
      } else {
        pendingOp = null; pendingIA = null; pendingG = false;
        pendingFind = null; pendingR = false;
        if (hlActive) { hlActive = false; refreshHighlights(); }
      }
      return true;
    }
    if (e.metaKey || e.ctrlKey || e.altKey) return false;   // 组合键放行

    let handled = false;
    if (mode === "NORMAL" && cfg.vim) handled = onNormalKey(e);
    else if (mode === "SEARCH") handled = onSearchKey(e);
    else handled = onInsertKey(e);
    if (handled) e.preventDefault();
    if (mode === "NORMAL" || cfg.touchCaret) { updateBlockCaret(); ensureCursorVisible(); }
    return handled;
  }

  // ---------- 程序注入（触屏虚拟键盘） ----------
  function keyShim(d) {   // 伪装成键盘事件的最小对象（只含管线读取的字段）
    return {
      key: d.key == null ? "" : d.key,
      code: d.code || "",
      shiftKey: !!d.shiftKey, ctrlKey: !!d.ctrlKey,
      metaKey: !!d.metaKey, altKey: !!d.altKey,
      preventDefault() {},
    };
  }
  // 物理键盘上「未 handled → 浏览器默认行为兜底」的键，注入时没有浏览器，在这里模拟：
  // INSERT 透传键（EN 全部可打印键 / CN 空缓冲的 Backspace·Delete·方向·Home/End 等）。
  function emulateDefault(e) {
    if (mode !== "INSERT") return;                    // NORMAL/SEARCH 没有可模拟的默认行为
    if (e.ctrlKey || e.metaKey || e.altKey) return;   // 组合键放行 = 无动作
    const key = e.key;
    if (key === "Shift" || key === "Control" || key === "Alt" || key === "Meta") return;
    const v = val(), s = textarea.selectionStart, t = textarea.selectionEnd;
    // 对齐 input 监听的外部编辑处理：推入编辑后快照 + 刷新高亮/行号
    const edit = (nv, nc) => { applyText(nv, nc, false); pushUndo(); };
    if (key === "Backspace") {
      preferredCol = null;
      if (s !== t) edit(v.slice(0, s) + v.slice(t), s);
      else if (s > 0) edit(v.slice(0, s - 1) + v.slice(t), s - 1);
      return;
    }
    if (key === "Delete") {
      preferredCol = null;
      if (s !== t) edit(v.slice(0, s) + v.slice(t), s);
      else if (s < v.length) edit(v.slice(0, s) + v.slice(s + 1), s);
      return;
    }
    if (key === "ArrowLeft") { preferredCol = null; setCur(s === t ? s - 1 : s); return; }
    if (key === "ArrowRight") { preferredCol = null; setCur(s === t ? t + 1 : t); return; }
    if (key === "Home") { preferredCol = null; setCur(lineStart(v, t)); return; }
    if (key === "End") { preferredCol = null; setCur(lineEnd(v, t)); return; }
    if (key === "ArrowUp" || key === "ArrowDown") {
      // 注：按逻辑行移动（textarea 原生按视觉行）；软折行下是近似
      const li = lineIndexOf(v, t);
      const t2 = key === "ArrowDown" ? li + 1 : li - 1;
      if (t2 < 0) { preferredCol = null; setCur(0); return; }
      if (t2 >= lineCount(v)) { preferredCol = null; setCur(v.length); return; }
      const col = preferredCol ?? (t - lineStart(v, t));
      preferredCol = col;
      const ts = lineStartByIndex(v, t2);
      setCur(Math.min(ts + col, lineEnd(v, ts)));
      return;
    }
    preferredCol = null;
    if (key === "Enter") { edit(v.slice(0, s) + "\n" + v.slice(t), s + 1); return; }
    if (key === "Tab") { edit(v.slice(0, s) + cfg.indent + v.slice(t), s + cfg.indent.length); return; }
    const ch = resolveChar(e);
    if (ch != null && ch.length === 1) edit(v.slice(0, s) + ch + v.slice(t), s + 1);
  }

  function onKeyup(e) {
    if (e.key === "Shift" && shiftLone) {
      shiftLone = false;
      if (mode === "INSERT" || mode === "SEARCH") {
        english = !english;
        if (english && composition) {   // 切英文：清缓冲
          abortSession();
          composition = ""; caret = 0;
          refreshSegment(); refreshCandidates(); render();
        }
        if (english && mode === "SEARCH" && searchComp) {  // 搜索框组合串原样入串
          searchBuf += searchComp; searchComp = "";
          renderSearch();
        }
        notifyMode();
      }
    }
  }

  function onScrollOrResize() {
    if (popup.style.display !== "none") render();
    updateBlockCaret();
    backdrop.scrollTop = textarea.scrollTop;
    backdrop.scrollLeft = textarea.scrollLeft;
  }

  textarea.addEventListener("keydown", onKeydown);
  textarea.addEventListener("focus", updateBlockCaret);
  textarea.addEventListener("blur", updateBlockCaret);
  textarea.addEventListener("click", () => { pendingComma = null; updateBlockCaret(); });
  textarea.addEventListener("scroll", () => {
    backdrop.scrollTop = textarea.scrollTop;
    backdrop.scrollLeft = textarea.scrollLeft;
    gutter.scrollTop = textarea.scrollTop;
  });
  document.addEventListener("keyup", onKeyup);
  window.addEventListener("scroll", onScrollOrResize, true);
  function onWindowResize() { onScrollOrResize(); refreshGutter(); }
  window.addEventListener("resize", onWindowResize);

  // caret 样式（注入一次）
  const style = document.createElement("style");
  style.textContent =
    ".ime-caret{display:inline-block;width:2px;height:1.1em;background:#4a90d9;" +
    "vertical-align:text-bottom;margin:0 -1px;animation:ime-blink 1s step-start infinite}" +
    "@keyframes ime-blink{50%{opacity:0}}" +
    ".ime-backdrop mark{background:#ffe58a;color:transparent;border-radius:2px}" +
    ".ime-backdrop mark.ime-hl-cur{background:#ffab4d}";
  document.head.appendChild(style);

  refreshGutter();
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(() => refreshGutter());   // web font 到位后重排行号高
  }

  // 初始模式
  if (cfg.vim) setMode("NORMAL");
  else setMode("INSERT");

  return {
    setOption(o) {
      const wasVim = cfg.vim;
      Object.assign(cfg, o);
      if (cfg.vim !== wasVim) {
        if (cfg.vim) setMode("NORMAL");
        else setMode("INSERT");
      }
      textarea.style.caretColor = (mode === "NORMAL" || cfg.touchCaret) ? "transparent" : "";
      updateBlockCaret();
      notifyMode();
    },
    // 订阅 mode/english 变化；返回退订函数
    onModeChange(fn) { modeListeners.push(fn); return () => {
      const i = modeListeners.indexOf(fn); if (i >= 0) modeListeners.splice(i, 1);
    }; },
    getMode: () => ({ mode, english, composing: !!composition }),
    focus: () => textarea.focus(),
    // 程序注入按键（触屏虚拟键盘）：与物理键盘完全同一管线；
    // 未 handled 的透传键由 emulateDefault 模拟浏览器默认行为。
    sendKey(d) {
      const e = keyShim(d);
      const handled = onKeydown(e);
      if (!handled) { emulateDefault(e); updateBlockCaret(); ensureCursorVisible(); }
    },
    sendKeyUp(d) { onKeyup(keyShim(d)); },   // Shift 单按切中英依赖 keyup
    getLayoutTable: () =>
      (typeof cfg.layout === "string" ? LAYOUTS[cfg.layout] : cfg.layout) || LAYOUTS.qwerty,
    getCaretPixel: (markerCh) => caretPixel(markerCh),
    destroy() {
      textarea.removeEventListener("keydown", onKeydown);
      textarea.removeEventListener("focus", updateBlockCaret);
      textarea.removeEventListener("blur", updateBlockCaret);
      textarea.removeEventListener("click", updateBlockCaret);
      document.removeEventListener("keyup", onKeyup);
      window.removeEventListener("scroll", onScrollOrResize, true);
      window.removeEventListener("resize", onWindowResize);
      textarea.style.caretColor = "";
      textarea.style.background = "";
      wrap.parentNode.insertBefore(textarea, wrap);   // 还原 DOM
      wrap.remove();
      popup.remove(); mirror.remove(); style.remove();
      blockCaret.remove(); gutterMirror.remove();
    },
  };
}

const ImeEditor = { attach, LAYOUTS, DEFAULT_MAPPINGS,
                    _pure: { chClass, motionW, motionB, motionE, findChar, matchPair,
                             quoteObject, bracketObject, wordObject, lineStart, lineEnd,
                             firstNonBlank } };
if (typeof module !== "undefined" && module.exports) module.exports = ImeEditor;
global.ImeEditor = ImeEditor;
})(typeof window !== "undefined" ? window : globalThis);
