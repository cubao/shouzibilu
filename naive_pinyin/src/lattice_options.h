// 词格解码参数（独立轻量头：config.h 与 lattice.h 共用，避免相互包含）。
#pragma once

namespace naive_pinyin {

// 评分公式移植自 msime（metasequoiaime/msime，GPL-3.0）
// crates/engine/src/lattice/decode.rs（其系数承自 libpinyin/sunpinyin 一脉
// 的公开设计并在 msime 实测扫参校准）。
struct LatticeOptions {
  bool enabled = true;  // false 时即使加载了 LM 也走无语言模型的 unigram DP

  int beam = 32;      // 每列保留的假设数
  int nbest = 6;      // 解出的整句路径数（trigram 重排需要比展示更多的路径）
  int max_entries_per_edge = 12;  // 每条词边摊进词格的词条上限（按分取前几）
  int max_alternates = 2;  // 主切分之外的备选切分数（每个递补 ≤2 条整句，
                           // 修 fangan 被最少段定成 fan|gan 后方案缺位）

  double bigram_weight = 2.0;    // bigram 增量权重（进搜索）
  double trigram_weight = 1.0;   // trigram 增量权重（对 n-best 重排）
  double phrase_bonus = 20.0;    // 多字词每音节覆盖奖励（msime 扫参：top1 0.850→0.900）

  // 词典 score（0..1000，log10 归一）→ ln 域的线性映射。
  // msime 原始量纲：单字 ln(w)-ln(1e6)（w 为语料字频，25 亿语料的「的」≈1e8），
  // 多字词 ln(w)+phrase_bonus×音节数（w 为词权重）。
  // 两档 span 同取 ln(1e8)（8105 字表的顶频字量级）：实测词组 span 低于
  // 单字时，高频词组被压低，「你好是饥饿」这类换切分的字链会顶掉
  // 「你好世界」（regression: nihaoshijie）。
  double unigram_char_span = 18.4;    // 单字分 1000 → 语料顶频字
  double unigram_char_floor = -13.8;  // -ln(1e6)：单字整体压到多字词之下
  double unigram_phrase_span = 18.4;  // 多字词分 1000 → 顶频词

  // 用户调频在 ln 域的加成：min(personal_max, 0.005 × boost) × personal_weight。
  // boost 是 0..600 的 score 域加分（UserFreq），0.005 使满档 ≈ 3.0 → 封顶 2.5。
  double personal_weight = 1.0;
  double personal_max = 2.5;
};

}  // namespace naive_pinyin
