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
