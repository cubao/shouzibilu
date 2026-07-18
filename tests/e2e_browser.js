// e2e 冒烟：手自笔录 ime-editor（依赖 playwright 与本机 Chromium）
//
// 运行：
//   make e2e                                  # 自起静态服务 + 跑全部用例
//   或手动：
//   python3 tools/serve.py 8971 &
//   E2E_PLAYWRIGHT=/path/to/node_modules node tests/e2e_browser.js
//
// 环境变量：
//   E2E_PLAYWRIGHT  playwright 所在 node_modules 路径（默认尝试全局 require）
//   E2E_CHROMIUM    Chromium 可执行文件路径（默认 ~/Library/Caches/ms-playwright 下探测）
//   E2E_PORT        静态服务端口（默认 8971，由脚本自起）

const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");

function loadPlaywright() {
  const candidates = [];
  if (process.env.E2E_PLAYWRIGHT) candidates.push(process.env.E2E_PLAYWRIGHT + "/playwright");
  candidates.push("playwright");
  for (const c of candidates) {
    try { return require(c); } catch (e) { /* next */ }
  }
  return null;
}

function findChromium() {
  if (process.env.E2E_CHROMIUM) return process.env.E2E_CHROMIUM;
  const base = path.join(process.env.HOME, "Library/Caches/ms-playwright");
  try {
    for (const dir of fs.readdirSync(base).sort().reverse()) {
      const p = path.join(base, dir, "chrome-mac/Chromium.app/Contents/MacOS/Chromium");
      if (fs.existsSync(p)) return p;
    }
  } catch (e) { /* fall through */ }
  return null;
}

const { chromium } = (() => {
  const pw = loadPlaywright();
  if (!pw) {
    console.log("SKIP: 找不到 playwright（设 E2E_PLAYWRIGHT 指向其 node_modules）");
    process.exit(0);
  }
  return pw;
})();

let failures = 0;
function check(name, cond, extra = "") {
  console.log((cond ? "PASS  " : "FAIL  ") + name + (cond ? "" : "  " + extra));
  if (!cond) failures++;
}

const PORT = process.env.E2E_PORT || 8971;

(async () => {
  const chromiumPath = findChromium();
  if (!chromiumPath) {
    console.log("SKIP: 找不到本机 Chromium（设 E2E_CHROMIUM）");
    process.exit(0);
  }

  // 自起静态服务（仓库根）
  const repoRoot = path.join(__dirname, "..");
  const server = spawn("python3", [path.join(repoRoot, "tools/serve.py"), String(PORT)],
                       { stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));

  const env = Object.assign({}, process.env);
  delete env.http_proxy; delete env.https_proxy;
  delete env.HTTP_PROXY; delete env.HTTPS_PROXY;
  const browser = await chromium.launch({ env, executablePath: chromiumPath });
  const page = await browser.newPage();
  page.on("pageerror", (e) => { console.log("PAGE ERROR:", e.message); failures++; });

  await page.goto(`http://localhost:${PORT}/`, { waitUntil: "load" });
  await page.waitForFunction(() =>
    document.getElementById("status").textContent.includes("词典"), { timeout: 30000 });

  const badge = () => page.textContent("#mode");
  const value = () => page.inputValue("#editor");
  const popupVisible = () => page.isVisible(".ime-popup");
  const popupText = () => page.textContent(".ime-popup").catch(() => "");
  // 找候选文字对应的序号（1-9），找不到返回 null
  const candIndex = async (text) => {
    return page.evaluate((t) => {
      const spans = document.querySelectorAll(".ime-popup .ime-cand");
      for (const s of spans) {
        const m = s.textContent.trim().match(/^(\d)\s*(.+)$/);
        if (m && m[2].startsWith(t)) return parseInt(m[1], 10);
      }
      return null;
    }, text);
  };

  // 统一用 QWERTY 布局测（按键名 = 字母）
  await page.selectOption("#layout", "qwerty");
  await page.click("#editor");

  // ---------- 0. 首访默认文字 / 控件 tooltip ----------
  check("首次打开显示介绍文字",
        (await value()).includes("手自笔录 · 网页里的中文输入法"));
  const hasTitles = await page.evaluate(() =>
    ["keyMode", "layout", "scheme", "vim", "fuzzy", "punct", "paging",
     "exportBtn", "importBtn", "clearBtn", "resetBtn"]
      .every((id) => {
        const el = document.getElementById(id);
        return el && (el.title || (el.closest("label") || {}).title);
      }));
  check("控件都有悬停说明", hasTitles);
  await page.evaluate(() => {   // 清场进入后续测试
    const ed = document.getElementById("editor");
    ed.value = ""; ed.dispatchEvent(new Event("input"));
  });

  // ---------- 1. vim 模式基础 ----------
  check("启动默认 NORMAL", (await badge()) === "NORMAL");
  await page.keyboard.press("i");
  check("i 进入 INSERT(中)", (await badge()) === "中");

  // ---------- 2. 组词光标学词全流程（wo de dk dp → 我的刀盾）----------
  await page.keyboard.type("wodedkdp");
  check("preedit 分词显示", (await popupText()).includes("wo de dk dp"));
  await page.keyboard.press("ArrowLeft");
  await page.keyboard.press("ArrowLeft");
  const idxWode = await candIndex("我的");
  check("光标移到 wode 后候选含「我的」", idxWode !== null);
  await page.keyboard.press(String(idxWode || 1));
  check("提交我的后缓冲剩 dk dp", (await popupText()).includes("dk dp"));
  await page.keyboard.press("ArrowLeft");
  const idxDao = await candIndex("刀");
  check("光标移到 dk 后候选含「刀」", idxDao !== null);
  await page.keyboard.press(String(idxDao));
  const idxDun = await candIndex("盾");
  check("缓冲 dp 候选含「盾」", idxDun !== null);
  await page.keyboard.press(String(idxDun));
  check("编辑器内容 = 我的刀盾", (await value()) === "我的刀盾", await value());
  await page.waitForTimeout(700);   // 学习数据防抖保存
  const learned = await page.evaluate(() => {
    const d = JSON.parse(localStorage.getItem("naive_pinyin.user_freq") || "[]");
    return d.some((e) => e.word === "我的刀盾" && e.pinyin === "wo de dao dun");
  });
  check("学会「我的刀盾」(存全拼 key wo de dao dun)", learned);
  // 学完后整词应直接成为首选
  await page.keyboard.type("wodedkdp");
  check("学完后整词浮到首选", (await candIndex("我的刀盾")) === 1,
        await popupText());
  await page.keyboard.press("Escape");

  // ---------- 3. 动态词 ----------
  await page.keyboard.type(",check");
  const t1 = await popupText();
  check(",check 出候选 ✅", t1.includes("✅"), t1);
  await page.keyboard.press(" ");   // 精确匹配上屏
  check("✅ 上屏", (await value()) === "我的刀盾✅", await value());
  await page.keyboard.type(",date");
  const t2 = await popupText();
  check(",date eval 出日期", /\d{4}-\d{2}-\d{2}/.test(t2), t2);
  await page.keyboard.press("Escape");
  check("Esc 清缓冲关弹窗", !(await popupVisible()));

  // ---------- 4. 光标处插入（原 bug：总插到末尾）----------
  await page.evaluate(() => {
    const ed = document.getElementById("editor");
    ed.value = "你好世界"; ed.selectionStart = ed.selectionEnd = 2; ed.focus();
  });
  await page.keyboard.type("12");
  check("数字插在光标处", (await value()) === "你好12世界", await value());

  // ---------- 5. vim 普通模式操作 ----------
  await page.keyboard.press("Escape");
  check("Esc 回 NORMAL", (await badge()) === "NORMAL");
  await page.keyboard.press("0");
  await page.keyboard.press("x");   // 删「你」
  check("x 删字", (await value()) === "好12世界", await value());
  await page.keyboard.press("u");
  check("u 撤销", (await value()) === "你好12世界", await value());
  await page.keyboard.press("d");
  await page.keyboard.press("d");   // dd
  check("dd 删行", (await value()) === "", await value());
  await page.keyboard.press("u");
  check("u 恢复行", (await value()) === "你好12世界", await value());

  // w/b/e：每汉字一词
  await page.keyboard.press("0");
  await page.keyboard.press("w");
  const cur1 = await page.evaluate(() => document.getElementById("editor").selectionStart);
  check("w 每汉字一词 (0->1)", cur1 === 1, String(cur1));

  // ---------- 6. % 配对跳 ----------
  await page.evaluate(() => {
    const ed = document.getElementById("editor");
    ed.value = "a(b(c)d)e"; ed.selectionStart = ed.selectionEnd = 1; ed.focus();
  });
  await page.keyboard.press("Shift+Digit5");   // % （playwright 直按 % 不带 shift 态）
  const cur2 = await page.evaluate(() => document.getElementById("editor").selectionStart);
  check("% 跳到配对外括号", cur2 === 7, String(cur2));

  // ---------- 7. 搜索（含中文 IME 搜索）----------
  await page.evaluate(() => {
    const ed = document.getElementById("editor");
    ed.value = "你好世界你好"; ed.selectionStart = ed.selectionEnd = 0; ed.focus();
  });
  await page.keyboard.press("/");
  check("/ 进 SEARCH", (await badge()) === "SEARCH");
  await page.keyboard.type("uijx");   // 自然码：世界
  await page.keyboard.press(" ");     // 首候选上屏到搜索串
  await page.keyboard.press("Enter");
  const cur3 = await page.evaluate(() => document.getElementById("editor").selectionStart);
  check("中文搜索「世界」跳转", cur3 === 2, String(cur3));
  check("搜索后回 NORMAL", (await badge()) === "NORMAL");

  // ---------- 8. dvorak4tzx 物理键映射 ----------
  await page.selectOption("#layout", "dvorak4tzx");
  await page.keyboard.press("Escape");   // 保险：确保 NORMAL（无缓冲时才是）
  check("当前 NORMAL", (await badge()) === "NORMAL");
  await page.keyboard.press("f");        // 物理 KeyF → dvorak4tzx 'i'
  check("物理 KeyF = i 进入 INSERT", (await badge()) === "中");
  await page.keyboard.press("h");        // 物理 KeyH → 'd'... 插入字母 d 进缓冲
  const t3 = await popupText();
  check("物理 KeyH = d 进缓冲", t3.includes("d"), t3);
  await page.keyboard.press("Escape");

  // ---------- 8.5 修复回归：dgg / cc / 动态词 Enter / yy+p / text object / f,t / >>,<< ----------
  await page.selectOption("#layout", "qwerty");   // 第 8 节切过 dvorak4tzx，切回来
  const setDoc = (v2, p) => page.evaluate(([v3, p3]) => {
    const ed = document.getElementById("editor");
    ed.value = v3; ed.selectionStart = ed.selectionEnd = p3; ed.focus();
    ed.dispatchEvent(new Event("input"));   // 触发 undo/高亮/行号刷新
  }, [v2, p]);
  const curPos = () => page.evaluate(() => document.getElementById("editor").selectionStart);

  await page.keyboard.press("Escape");
  await setDoc("aaa\nbbb\nccc", 6);   // 光标在 bbb 行
  await page.keyboard.press("d");
  await page.keyboard.press("g");
  await page.keyboard.press("g");       // dgg
  check("dgg 删到文首", (await value()) === "ccc", await value());
  await page.keyboard.press("u");
  check("u 恢复 dgg", (await value()) === "aaa\nbbb\nccc", await value());

  await setDoc("hello\nworld", 2);
  await page.keyboard.press("c");
  await page.keyboard.press("c");       // cc
  check("cc 清行留行进 INSERT", (await value()) === "\nworld" && (await badge()) === "中",
        (await value()) + "|" + await badge());
  await page.keyboard.press("Escape");

  await setDoc("", 0);
  await page.keyboard.press("i");
  await page.keyboard.type(",check");
  await page.keyboard.press("Enter");   // 动态词 Enter = 上屏
  check("动态词 Enter 上屏", (await value()) === "✅", await value());
  await page.keyboard.press("Escape");

  await setDoc("abc", 1);
  await page.keyboard.press("y");
  await page.keyboard.press("y");       // yy
  await page.keyboard.press("p");       // p
  check("yy + p 行式复制粘贴", (await value()) === "abc\nabc", await value());

  await setDoc("你好世界", 1);
  await page.keyboard.press("d");
  await page.keyboard.press("i");
  await page.keyboard.press("w");       // diw（汉字=一词）
  check("diw 删单汉字", (await value()) === "你世界", await value());

  await setDoc("abcdef", 0);
  await page.keyboard.press("f");
  await page.keyboard.press("c");       // fc
  check("f 找字符", (await curPos()) === 2, String(await curPos()));
  await setDoc("abcdef", 0);
  await page.keyboard.press("t");
  await page.keyboard.press("c");       // tc
  check("t 停在字符前", (await curPos()) === 1, String(await curPos()));
  await setDoc("abcdef", 0);
  await page.keyboard.press("d");
  await page.keyboard.press("f");
  await page.keyboard.press("c");       // dfc
  check("dfc 删到字符(含)", (await value()) === "def", await value());

  await setDoc("ab", 0);
  await page.keyboard.press("Shift+Period");
  await page.keyboard.press("Shift+Period");   // >> （playwright 直按 > 不带 shift 态）
  check(">> 缩进 4 空格", (await value()) === "    ab", await value());
  await page.keyboard.press("Shift+Comma");
  await page.keyboard.press("Shift+Comma");    // <<
  check("<< 反缩进", (await value()) === "ab", await value());

  // 搜索往返后 undo 仍正常（快照标记复位）
  await setDoc("你好世界你好", 0);
  await page.keyboard.press("/");
  await page.keyboard.type("uijx");
  await page.keyboard.press(" ");
  await page.keyboard.press("Enter");
  await page.keyboard.press("i");
  await page.keyboard.type("12");
  await page.keyboard.press("Escape");
  await page.keyboard.press("u");
  check("搜索后 undo 正常", (await value()) === "你好世界你好", await value());

  // ---------- 9. 翻页键与 , 不冲突 ----------
  await page.selectOption("#layout", "qwerty");
  await page.keyboard.press("Escape");
  await page.keyboard.press("i");
  await page.keyboard.type("ni");
  const page1 = await popupText();
  await page.keyboard.press(".");        // 拼音缓冲内 . 翻下一页
  const page2 = await popupText();
  check("拼音内 . 翻页", page1 !== page2 && page2.includes("2/"), page2);
  await page.keyboard.press("Escape");

  // ---------- 10. 逗号新语义：字面逗号 + 紧跟字母才进动态词 ----------
  const dynCandCount = () => page.evaluate(() =>
    document.querySelectorAll(".ime-popup .ime-cand").length);

  await setDoc("", 0);
  await page.keyboard.press("Escape");
  await page.keyboard.press("i");
  await page.keyboard.type(",");
  check(", 立即上屏字面逗号、无模式", (await value()) === "，" && !(await popupVisible()),
        await value());
  await page.keyboard.press(" ");
  check(", + 空格 = 逗号+空格（空格不丢）", (await value()) === "， ", await value());
  await setDoc("", 0);
  await page.keyboard.type(",5");
  check(", + 数字 = 字面逗号+数字", (await value()) === "，5", await value());
  await setDoc("", 0);
  await page.keyboard.type(",check");
  check(", + 字母：删逗号进动态词菜单", (await dynCandCount()) > 0);
  await page.keyboard.press(" ");
  check("动态词上屏（前面无逗号残留）", (await value()) === "✅", await value());
  // 动态词退格到裸逗号再退出：回到无模式
  await page.keyboard.type(",date");
  check(",date 菜单在", (await dynCandCount()) > 0);
  await page.keyboard.press("Escape");
  check("Esc 退出动态词", !(await popupVisible()));

  // EN 模式：正常打英文完全无感；,check 照样生效
  await setDoc("", 0);
  await page.keyboard.press("Shift");   // EN
  await page.keyboard.type("hello, world");
  check("EN hello, world 无干扰", (await value()) === "hello, world", await value());
  await setDoc("", 0);
  await page.keyboard.type(",check");
  check("EN ,check 出菜单", (await dynCandCount()) > 0);
  await page.keyboard.press(" ");
  check("EN ,check 上屏", (await value()) === "✅", await value());
  await page.keyboard.press("Shift");   // 切回中文

  // ---------- 10.5 布局撑满 / 光标可见 / 行号 ----------
  const wrapInfo = await page.evaluate(() => {
    const w = document.querySelector(".ime-wrap");
    const ed = document.getElementById("editor");
    return { wrap: !!w, edH: ed.clientHeight, g: !!document.querySelector(".ime-gutter") };
  });
  check("编辑器撑满 (wrap+gutter 存在, 高度>240)",
        wrapInfo.wrap && wrapInfo.g && wrapInfo.edH > 240, JSON.stringify(wrapInfo));

  // 光标可见性：50 行文档，G 到底后 scrollTop > 0，gg 回顶后 ≈0
  await page.keyboard.press("Escape");
  await setDoc(Array.from({length: 50}, (_, i) => "第" + (i + 1) + "行").join("\n"), 0);
  await page.keyboard.press("g");
  await page.keyboard.press("g");   // gg 到顶
  const st1 = await page.evaluate(() => document.getElementById("editor").scrollTop);
  await page.keyboard.press("Shift+g");   // G 到底
  const st2 = await page.evaluate(() => document.getElementById("editor").scrollTop);
  check("G 后滚动到底部可见光标", st2 > st1, `${st1} -> ${st2}`);
  await page.keyboard.press("g");
  await page.keyboard.press("g");   // gg 回顶
  const st3 = await page.evaluate(() => document.getElementById("editor").scrollTop);
  check("gg 后滚回顶部", st3 < st2 && st3 < 50, String(st3));

  // 行号：数量与逻辑行一致、滚动同步
  const gInfo = await page.evaluate(() => {
    const g = document.querySelector(".ime-gutter");
    return { rows: g.children.length, last: g.lastElementChild.textContent,
             st: g.scrollTop, edSt: document.getElementById("editor").scrollTop };
  });
  check("行号数量 = 逻辑行数", gInfo.rows === 50 && gInfo.last === "50",
        JSON.stringify(gInfo));
  await page.keyboard.press("Shift+g");
  const gSync = await page.evaluate(() => {
    const g = document.querySelector(".ime-gutter");
    return Math.abs(g.scrollTop - document.getElementById("editor").scrollTop);
  });
  check("行号滚动同步", gSync < 2, String(gSync));
  await setDoc("", 0);
  await page.keyboard.press("Escape"); await page.keyboard.press("i");
  await page.keyboard.press("Escape");

  // block 光标
  check("NORMAL 显示 block 光标", await page.isVisible(".ime-block-caret"));
  const cc = await page.evaluate(() => document.getElementById("editor").style.caretColor);
  check("NORMAL 隐藏原生细光标", cc === "transparent", cc);
  await page.keyboard.press("i");
  check("INSERT 隐藏 block 光标", !(await page.isVisible(".ime-block-caret")));
  await page.keyboard.press("Escape");

  // >> 后光标在首个非空白
  await setDoc("ab", 0);
  await page.keyboard.press("Shift+Period");
  await page.keyboard.press("Shift+Period");
  check(">> 后光标在 ^ 处", (await curPos()) === 4, String(await curPos()));
  await setDoc("", 0);

  // ---------- 11. 搜索修复：en/cn 遵循 / 候选列表 / 实时高亮 / Esc 取消与清除 ----------
  const hlCount = () => page.evaluate(() =>
    document.querySelectorAll(".ime-backdrop mark").length);

  // EN 模式搜索 = 字面输入，不出候选
  await setDoc("bar foo bar", 0);
  await page.keyboard.press("i");
  await page.keyboard.press("Shift");   // EN
  await page.keyboard.press("Escape");  // NORMAL（english 标记保持）
  await page.keyboard.press("/");
  await page.keyboard.type("bar");
  check("EN 搜索无候选菜单", (await dynCandCount()) === 0);
  check("EN 搜索实时高亮", (await hlCount()) === 2, String(await hlCount()));
  check("incsearch 已跳转", (await curPos()) === 0, String(await curPos()));
  await page.keyboard.press("Enter");
  check("EN 搜索接受后回 NORMAL", (await badge()) === "NORMAL");
  check("接受后高亮保留", (await hlCount()) === 2);
  await page.keyboard.press("n");
  check("n 跳到下一个", (await curPos()) === 8, String(await curPos()));
  await page.keyboard.press("Escape");
  check("NORMAL Esc 清高亮", (await hlCount()) === 0);
  await page.keyboard.press("i");
  await page.keyboard.press("Shift");   // 切回中文
  await page.keyboard.press("Escape");  // NORMAL

  // CN 搜索出候选列表 + Esc 取消还原
  await setDoc("你好世界你好", 0);
  await page.keyboard.press("/");
  await page.keyboard.type("uijx");
  check("CN 搜索出候选列表", (await dynCandCount()) > 0);
  const idxSj = await candIndex("世界");
  check("CN 搜索候选含世界", idxSj !== null);
  await page.keyboard.press(String(idxSj || 1));
  check("候选入搜索串后高亮", (await hlCount()) === 1, String(await hlCount()));
  await page.keyboard.press("Escape");
  check("Esc 取消还原光标", (await curPos()) === 0, String(await curPos()));
  check("Esc 取消清高亮", (await hlCount()) === 0);
  check("Esc 取消回 NORMAL", (await badge()) === "NORMAL");

  // ? 反向搜索
  await setDoc("ab ab ab", 8);
  await page.keyboard.press("Shift+Slash");   // ? （playwright 直按 ? 不带 shift 态）
  await page.keyboard.press("Shift");   // 搜索内 Shift 切 EN（跟随 en/cn 状态）
  await page.keyboard.type("ab");
  check("? 反向 incsearch", (await curPos()) === 6, String(await curPos()));
  await page.keyboard.press("Enter");
  check("? 接受", (await badge()) === "NORMAL");
  await page.keyboard.press("Escape");  // 清高亮
  await setDoc("", 0);
  // EN 标记复位（ Shift 在搜索里切了一次，现在是 EN；切回中文）
  await page.keyboard.press("i");
  await page.keyboard.press("Shift");
  await page.keyboard.press("Escape");

  // ---------- 12. A / I / r / block 光标 blend / 持久化 ----------
  // A：行尾插入
  await setDoc("hello\nworld", 1);
  await page.keyboard.press("Shift+A");
  check("A 跳行尾进 INSERT", (await curPos()) === 5 && (await badge()) === "中",
        (await curPos()) + "|" + await badge());
  await page.keyboard.press("Escape");

  // I：首个非空白插入
  await setDoc("  hello", 5);
  await page.keyboard.press("Shift+I");
  check("I 跳 ^ 进 INSERT", (await curPos()) === 2 && (await badge()) === "中",
        (await curPos()) + "|" + await badge());
  await page.keyboard.press("Escape");

  // r<char>：替换单字符，留 NORMAL
  await setDoc("abc", 1);
  await page.keyboard.press("r");
  await page.keyboard.press("x");
  check("r 替换单字符", (await value()) === "axc" && (await badge()) === "NORMAL",
        (await value()) + "|" + await badge());
  check("r 后光标不动", (await curPos()) === 1, String(await curPos()));
  await page.keyboard.press("u");
  check("u 撤销 r", (await value()) === "abc", await value());

  // r<Enter>：拆行
  await setDoc("abc", 1);
  await page.keyboard.press("r");
  await page.keyboard.press("Enter");
  check("r<Enter> 拆行", (await value()) === "a\nc", await value());

  // r 在换行符上：拒绝
  await setDoc("a\nb", 1);
  await page.keyboard.press("r");
  await page.keyboard.press("x");
  check("r 在 \\n 上无操作", (await value()) === "a\nb", await value());

  const blend = await page.evaluate(() => {
    const b = document.querySelector(".ime-block-caret");
    return { bm: b.style.mixBlendMode, text: b.textContent };
  });
  check("block 光标是 difference 混合、无文本重绘",
        blend.bm === "difference" && blend.text === "");

  // 持久化：写内容 + 改映射草稿 → pagehide → 重载恢复
  await setDoc("持久化测试内容", 0);
  await page.evaluate(() => {
    document.getElementById("mappings").value = '{",draft-test": "✍"}';
    window.dispatchEvent(new Event("pagehide"));
  });
  await page.reload({ waitUntil: "load" });
  await page.waitForFunction(() =>
    document.getElementById("status").textContent.includes("词典"), { timeout: 30000 });
  check("重载后编辑器内容恢复", (await value()) === "持久化测试内容", await value());
  const taVal = await page.inputValue("#mappings");
  check("重载后映射草稿恢复", taVal.includes("draft-test"), taVal);
  await setDoc("", 0);
  await page.evaluate(() => window.dispatchEvent(new Event("pagehide")));

  // ---------- 13. 重置按钮恢复默认文字 ----------
  await setDoc("被覆盖的内容", 0);
  await page.evaluate(() => window.dispatchEvent(new Event("pagehide")));
  await page.reload({ waitUntil: "load" });
  await page.waitForFunction(() =>
    document.getElementById("status").textContent.includes("词典"), { timeout: 30000 });
  check("重置前内容已持久化", (await value()) === "被覆盖的内容", await value());
  page.once("dialog", (d) => d.accept());
  await page.click("#resetBtn");
  check("重置恢复介绍文字",
        (await value()).includes("手自笔录 · 网页里的中文输入法"));
  check("重置后立即持久化", await page.evaluate(() =>
    localStorage.getItem("shouzibilu.content").includes("手自笔录")));

  await browser.close();
  server.kill();
  console.log("----");
  console.log(failures === 0 ? "OK: all e2e tests passed" : `FAILED: ${failures}`);
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error("e2e crashed:", e); process.exit(1); });

process.on("SIGINT", () => process.exit(1));
