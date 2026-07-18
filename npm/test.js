// npm 包封装自测：node npm/test.js
"use strict";
const { createEngine, ziranma } = require("./index.js");

function assert(cond, msg) {
  if (!cond) throw new Error("断言失败: " + msg);
  console.log("PASS ", msg);
}

(async () => {
  const engine = await createEngine({
    shuangpin: ziranma.shuangpin,
    fuzzy: [["z", "zh"]],
    max_candidates: 8,
  });

  const r1 = engine.query("nihkuijx");
  assert(r1.candidates[0].text === "你好世界",
         "双拼 nihkuijx -> " + r1.candidates[0].text);

  const r2 = engine.query("vshwrfmngshego");
  assert(r2.candidates[0].text === "中华人民共和国",
         "双拼长词 -> " + r2.candidates[0].text);

  // 调频: jnnj = 济南(自然码)
  const before = engine.query("jnnj").candidates;
  const other = before.find(c => c.text !== before[0].text);
  engine.commit(other.segments);
  const after = engine.query("jnnj").candidates;
  assert(after[0].text === other.text,
         `调频 ${other.text} 登顶`);

  // 自造词: thvixs = 唐志雄(自然码)
  engine.learnWord("tang zhi xiong", "唐志雄");
  const r3 = engine.query("thvixs");
  assert(r3.candidates[0].text === "唐志雄", "自造词进入候选");

  const dump = engine.dumpUser();
  assert(dump.some(e => e.word === "唐志雄" && e.pinyin === "tang zhi xiong"),
         `导出自造词 (共 ${dump.length} 条)`);

  // segment: 双拼恒 2 键一站 + 主切分路径
  const seg = engine.segment("wodedkdp");
  assert(JSON.stringify(seg.boundaries) === "[0,2,4,6,8]",
         "segment 双拼 DAG 边界");
  assert(JSON.stringify(seg.path) === "[0,2,4,6,8]",
         "segment 主切分路径");

  // imeEditor 导出（Node 下只加载不 attach，不碰 DOM）
  const { imeEditor } = require("./index.js");
  assert(imeEditor && typeof imeEditor.attach === "function" &&
         imeEditor.LAYOUTS.dvorak4tzx && imeEditor.DEFAULT_MAPPINGS[",check"],
         "imeEditor 导出可用");

  engine.destroy();
  console.log("----\nnpm package test: OK");
})().catch(e => {
  console.error("FAILED:", e.message);
  process.exit(1);
});
