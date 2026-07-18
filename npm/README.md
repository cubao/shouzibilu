# @cubao/naive-pinyin

字母串 → 汉字候选的精简拼音引擎（WebAssembly）。手自笔录项目核心库。

- 全拼 + 双拼（内置自然码码表，任意方案可配）
- 整句输入：自动音节切分 + DP 最优路径
- 模糊音（可配）、动态调频、自造词学习
- wasm 仅 ~160KB，内置精简词库（雾凇拼音 + jieba 词频，4.2MB）

## Node

```js
const { createEngine, ziranma } = require("@cubao/naive-pinyin");

const engine = await createEngine({
  shuangpin: ziranma.shuangpin,          // 双拼；缺省为全拼
  fuzzy: [["z", "zh"], ["in", "ing"]],   // 模糊音
  max_candidates: 10,
});

engine.query("nihkuijx").candidates[0].text;   // => 你好世界
engine.query("vshwrfmngshego").candidates[0].text; // => 中华人民共和国

// 动态调频：提交候选分段，下次排序提前
const cand = engine.query("jinan").candidates[0];
engine.commit(cand.segments);

// 自造词
engine.learnWord("tang zhi xiong", "唐志雄");

// 持久化：导出叠加层，下次经 config.user_freq 回传
const saved = engine.dumpUser();
engine.destroy();

// 音节边界（组词光标）：
engine.segment("wodedkdp");
// => { input, boundaries: [0,2,4,6,8], path: [0,2,4,6,8] }
// boundaries = 音节 DAG 全边界（歧义切分的所有边界都可达）；
// path = 贪心最长边主切分（preedit 分词显示用）。
```

## 浏览器 IME 编辑器（ime-editor）

包内附 `ime-editor.js`：接管 `<textarea>` 全部键盘输入的完整编辑器——
物理键/字母双模式（布局表 qwerty/dvorak/dvorak4tzx，可自定义）、
候选弹窗悬停光标、组词光标学词、动态词（`,date` → 日期）、
精简 vim（Normal/Insert/Search，operator × motion / text-object）。

```js
const { createEngine, ziranma, imeEditor } = require("@cubao/naive-pinyin");
const engine = await createEngine({ shuangpin: ziranma.shuangpin });
// Engine 实例的方法名与编辑器适配器一一对应
imeEditor.attach(document.querySelector("textarea"), {
  getEngine: () => engine,
  layout: "dvorak4tzx",     // qwerty | dvorak | dvorak4tzx | 自定义表
  keyMode: "physical",      // physical(e.code+布局表) | letter(e.key)
  vim: true,
  mappings: { ",check": "✅",
              ",date": "eval:return new Date().toLocaleDateString('sv')" },
});
```

### 触屏虚拟键盘（virtual-keyboard，可选伴侣）

包内附 `virtual-keyboard.js`：页面底部上拉展开的虚拟键盘，键面随布局表渲染，
按键经 `ime.sendKey` 注入，与物理键盘完全同一管线。
左栏仿真实键盘左缘（\`~ Tab ESC Shift Ctrl + 数字/符号两列），
Shift/Ctrl 为 sticky 单发，中/EN 专用键切换，长按 ←/→ = Home/End，
小红点长按 = 放大镜拖动光标。触屏打开时 textarea 置 readonly 屏蔽系统键盘。

```js
const { imeEditor, virtualKeyboard } = require("@cubao/naive-pinyin");
const ime = imeEditor.attach(document.querySelector("textarea"), { ... });
const vkb = virtualKeyboard.attach(ime, document.querySelector("textarea"), {
  coarse: matchMedia("(pointer: coarse)").matches,   // 触屏时才 readonly/自绘光标
});
vkb.setEnabled(true);   // 显示上拉手柄（vkb.refresh() 可在布局变更后重渲键面）
```

## 浏览器（bundler）

```js
import { createEngine, ziranma } from "@cubao/naive-pinyin";

const dictUrl = new URL(
  "@cubao/naive-pinyin/naive_pinyin.dict.txt", import.meta.url);
const dict = await fetch(dictUrl).then(r => r.arrayBuffer());
const engine = await createEngine({ shuangpin: ziranma.shuangpin },
                                  new Uint8Array(dict));
```

## 说明

- 输入只接受小写字母与 `'`（音节分隔符）；标点、键盘布局、候选 UI
  都是上层的事，本库只做「字母串 → 候选」。
- 完整演示：https://district10.github.io/shouzibilu/
- 源码：https://github.com/district10/shouzibilu

## 许可与署名

- 代码：MIT License
- 内置词库衍生自 [雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice)
  （CC-BY 4.0）与 [jieba](https://github.com/fxsjy/jieba) 词典（MIT）
- 算法受 [librime](https://github.com/rime/librime)（BSD）启发重写
