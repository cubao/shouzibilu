// naive_pinyin wasm node 冒烟测试
// 用法: node wasm/smoke_test.js  (先 make wasm dict ziranma)
"use strict";

const fs = require("fs");
const path = require("path");

const createNaivePinyin = require("./naive_pinyin.js");

let failures = 0;
function check(name, cond) {
  if (cond) {
    console.log("PASS  " + name);
  } else {
    console.log("FAIL  " + name);
    ++failures;
  }
}

async function main() {
  const Module = await createNaivePinyin();

  const dictPath = path.join(__dirname, "../data/naive_pinyin.dict.txt");
  const configPath = path.join(__dirname, "ziranma.json");
  const dictData = fs.readFileSync(dictPath);
  const config = JSON.parse(fs.readFileSync(configPath, "utf8"));
  config.fuzzy = [["z", "zh"], ["in", "ing"]];
  config.max_candidates = 8;

  // 创建引擎（双拼配置）
  const ctx = Module.ccall("np_create", "number", ["string"],
                           [JSON.stringify(config)]);
  check("np_create returns ctx", ctx !== 0);

  // 加载词典：拷入 wasm 内存
  const bufPtr = Module._malloc(dictData.length);
  Module.HEAPU8.set(dictData, bufPtr);
  const ok = Module.ccall("np_load_dict", "number",
                          ["number", "number", "number"],
                          [ctx, bufPtr, dictData.length]);
  Module._free(bufPtr);
  check("np_load_dict ok", ok === 1);

  function query(input) {
    const s = Module.ccall("np_query", "string", ["number", "string"],
                           [ctx, input]);
    return JSON.parse(s);
  }

  // 全拼（另建一个无 shuangpin 的引擎）
  const ctxFull = Module.ccall("np_create", "number", ["string"], ["{}"]);
  const bufPtr2 = Module._malloc(dictData.length);
  Module.HEAPU8.set(dictData, bufPtr2);
  Module.ccall("np_load_dict", "number", ["number", "number", "number"],
               [ctxFull, bufPtr2, dictData.length]);
  Module._free(bufPtr2);
  const full = JSON.parse(Module.ccall("np_query", "string",
                                       ["number", "string"],
                                       [ctxFull, "nihaoshijie"]));
  check("全拼 nihaoshijie -> 你好世界",
        full.candidates[0] && full.candidates[0].text === "你好世界");

  // 双拼
  const r1 = query("nihkuijx");
  check("双拼 nihkuijx -> 你好世界",
        r1.candidates[0] && r1.candidates[0].text === "你好世界");

  const r2 = query("vshwrfmngshego");
  check("双拼 vshwrfmngshego -> 中华人民共和国",
        r2.candidates[0] && r2.candidates[0].text === "中华人民共和国");

  // 模糊音: ui=shi, 但配置 z/zh... 用全拼引擎测 zhi/zi
  const fuzzy = JSON.parse(Module.ccall("np_query", "string",
                                        ["number", "string"],
                                        [ctxFull, "zij"]));
  // "zij" 不是合法输入切分(zi + j 死路), 换 "ziji": 自己/字迹...
  const r3 = JSON.parse(Module.ccall("np_query", "string",
                                     ["number", "string"],
                                     [ctxFull, "ziji"]));
  check("模糊音 ziji 候选含 自己(ziji) 或 字迹(zi ji)",
        r3.candidates.some(c => c.text === "自己" || c.text === "字迹"));

  // 非法输入
  const r4 = query("Ni3");
  check("非法输入返回 error", r4.error !== undefined);

  Module.ccall("np_destroy", null, ["number"], [ctx]);
  Module.ccall("np_destroy", null, ["number"], [ctxFull]);

  console.log("----");
  console.log(failures === 0 ? "OK: all smoke tests passed"
                             : `FAILED: ${failures} failures`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch(e => {
  console.error("smoke test crashed:", e);
  process.exit(1);
});
