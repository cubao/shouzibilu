// @cubao/naive-pinyin - 薄封装：隐藏 emscripten ccall 样板
//
// Node:
//   const { createEngine, ziranma } = require('@cubao/naive-pinyin');
//   const engine = await createEngine({ shuangpin: ziranma.shuangpin });
//   engine.query('nihkuijx').candidates[0].text  // => 你好世界
//
// 浏览器 (bundler):
//   import { createEngine, ziranma } from '@cubao/naive-pinyin';
//   const dict = await fetch(require.resolve('@cubao/naive-pinyin/naive_pinyin.dict.txt'))
//     .then(r => r.arrayBuffer());
//   const engine = await createEngine({ shuangpin: ziranma.shuangpin }, dict);
"use strict";

const createNaivePinyin = require("./naive_pinyin.js");
const ziranma = require("./ziranma.json");

let modulePromise = null;
function loadModule() {
  if (!modulePromise) modulePromise = createNaivePinyin();
  return modulePromise;
}

// Node 下读取包内词典；浏览器请自行 fetch 后传入 createEngine 第二参数。
function loadBundledDict() {
  if (typeof window !== "undefined") {
    throw new Error(
      "浏览器环境请 fetch 词典后传给 createEngine(config, dictData)");
  }
  const fs = require("fs");
  const path = require("path");
  return fs.readFileSync(path.join(__dirname, "naive_pinyin.dict.txt"));
}

class Engine {
  constructor(M, ctx) {
    this.M = M;
    this.ctx = ctx;
  }

  // config: { shuangpin?, fuzzy?, max_candidates?, segment_penalty?,
  //           user_boost?, user_freq?, user_words? }
  // dictData: Uint8Array/Buffer（缺省时 Node 自动读包内词典）
  static async create(config = {}, dictData) {
    const M = await loadModule();
    if (!dictData) dictData = loadBundledDict();
    const ctx = M.ccall("np_create", "number", ["string"],
                        [JSON.stringify(config)]);
    if (!ctx) throw new Error("np_create failed: 配置 JSON 有误");
    const ptr = M._malloc(dictData.length);
    M.HEAPU8.set(dictData, ptr);
    const ok = M.ccall("np_load_dict", "number",
                       ["number", "number", "number"],
                       [ctx, ptr, dictData.length]);
    M._free(ptr);
    if (ok !== 1) {
      M.ccall("np_destroy", null, ["number"], [ctx]);
      throw new Error("np_load_dict failed: 词典数据有误");
    }
    return new Engine(M, ctx);
  }

  // { input, candidates: [{text, consumed, score, segments:[{key,word}]}] }
  query(input) {
    const s = this.M.ccall("np_query", "string", ["number", "string"],
                           [this.ctx, input]);
    return JSON.parse(s);
  }

  // 音节边界（组词光标）：{ input, boundaries, path }
  // boundaries = 音节 DAG 上从起点可达的所有位置（光标站位）；
  // path = 贪心最长边主切分（preedit 分词显示用）。
  segment(input) {
    const s = this.M.ccall("np_segment", "string", ["number", "string"],
                           [this.ctx, input]);
    return JSON.parse(s);
  }

  // 提交候选分段（动态调频）。segments 取自 query 结果。
  commit(segments) {
    this.M.ccall("np_commit", null, ["number", "string"],
                 [this.ctx, JSON.stringify(segments)]);
  }

  // 学习自造词。key 为空格分隔音节，如 "tang zhi xiong"。
  learnWord(key, word) {
    this.M.ccall("np_learn_word", null, ["number", "string", "string"],
                 [this.ctx, key, word]);
  }

  // 导出用户叠加层 [{pinyin, word, count}]，供持久化；
  // 下次经 config.user_freq 回传即恢复。
  dumpUser() {
    const s = this.M.ccall("np_dump_user", "string", ["number"], [this.ctx]);
    return JSON.parse(s);
  }

  destroy() {
    if (this.ctx) {
      this.M.ccall("np_destroy", null, ["number"], [this.ctx]);
      this.ctx = 0;
    }
  }
}

module.exports = {
  Engine,
  ziranma,
  createEngine: (config, dictData) => Engine.create(config, dictData),
  // 浏览器 IME 编辑器（textarea 接管 + 候选弹窗 + 动态词 + 精简 vim）。
  // 懒加载：require 时不碰 DOM，仅在浏览器中 attach 时才需要。
  get imeEditor() { return require("./ime-editor.js"); },
};
