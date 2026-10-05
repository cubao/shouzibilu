# AGENTS.md — 给 AI 代理/新协作者的项目速览

手自笔录 (shǒuzìbǐlù)：C++17 拼音引擎 `naive_pinyin`（词格 beam 解码 +
bigram 语言模型），编译到 WebAssembly，提供网页输入法（`index.html`）、
npm 包（`@cubao/naive-pinyin`）与 macOS 输入法（`macos/`）三种使用形态。

## 关键事实（改代码前先读）

- **许可证：GPL-3.0**（见 `LICENSE`）。候选引擎 v2（词格解码、评分系数、
  MSNG 表格式）移植自 [msime](https://github.com/metasequoiaime/msime)
  （GPL-3.0），落点文件头有逐个署名：`naive_pinyin/src/lattice*.{h,cc}`、
  `ngram.{h,cc}`、`lattice_options.h`。别把这些改成宽松许可。
- **词表来自 msime 官方词典** `msime-pinyin.db`（dict-v2.0.7，~74MB SQLite，
  sha256 见 `tools/msime-dict.sha256`；`make msime-dict-download` 下载）。
  `data/naive_pinyin.dict.txt` 是转换产物（`tools/build_dict_msime.py`），
  已入库。**不要**引入 rime-ice/jieba/tencent 词源——那条管线已废弃。
  转换公式 `score = 1000·ln(weight)/18.4` 与引擎词格评分逐字一致
  （对应 msime unigram_z=1e6、phrase_length_bonus=20），改动要两边同步。
- **语言模型**：msime 以中文维基百科语料统计的 MSNG v1 表（bigram/trigram，
  FNV-1a 64 键 + f32 增量，值域 ±3）。仓库内置 bigram 紧凑档
  `data/naive_pinyin.bigram.bin`（|增量|≥1.0 子集，6.4MB）；
  `make lm-download` 取全量档。再分发须保留维基百科署名（CC-BY-SA 4.0），
  见 README「数据来源与许可」。
- **协议是契约**：`np_query` 返回的 JSON（text/consumed/score/segments）
  被 web/npm/macOS 三端消费；改字段要三端同步。C API 新函数记得加进
  Makefile 的 `EXPORTED_FUNCTIONS`。
- **无 LM 时行为 = 旧版**：bigram 未加载或校验失败，引擎自动退回
  unigram DP。这是「用户不受影响」的兜底，别破坏。

## 常用命令

```sh
make test          # native 单元测试（日常开发主用）
make regression    # 排序回归：无 LM 21 条；--lm data/naive_pinyin.bigram.bin 加 11 条整句断言
make dict          # 由 data/msime-pinyin.db 重建词典（先 msime-dict-download）
make wasm && make smoke   # wasm 构建 + node 冒烟
make npm && make npm-test # 组装/自测 npm 包；发布 make npm-publish
make macos         # macOS 输入法（make macos-install 安装）
```

注意：本机构建 native 要 `export SDKROOT=$(xcrun -sdk macosx --show-sdk-path)`
（CommandLineTools 27.0 SDK 的 tbd 与当前 clang 不兼容）。

## 发布清单

1. `npm/package.json` 升版本（semver）；`make npm npm-test` 自测；
   `make npm-publish`（官方 registry，granular token 已配置）。
2. `make wasm`（会顺带把版本戳 `tools/stamp_build.py` 写进 index.html 的
   BUILD_INFO，页面右上方展示构建日期+commit，用户可辨认缓存新旧）。
3. wasm 产物（`wasm/naive_pinyin.{js,wasm}`）与词典/模型紧凑档都是
   **入库的**（GitHub Pages 直接部署仓库根），改了必须一并提交。
