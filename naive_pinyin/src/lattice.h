// 词格（lattice）beam 解码：音节 DAG × 词典词条 → N-best 整句路径。
//
// 算法移植自 msime（metasequoiaime/msime，GPL-3.0）
// crates/engine/src/lattice/decode.rs（其设计参照 libpinyin PinyinLookup2
// 与 sunpinyin lattice 的公开结构）：
//   - 逐列扩展，每列按分数剪枝到 beam；
//   - 每条转移 = 词格边基础分 + bigram_weight × bigram 增量；
//   - beam 假设只带一词历史，trigram 不进搜索，解出 n-best 后逐条加分
//     重排（所以要解出比展示更多的路径）；
//   - 句首前词用 kNgramSentenceStart。
//
// 评分量纲：词典 score（0..1000，log10 归一）经 LatticeOptions 线性映射
// 到 ln 域；无 bigram 表时不应进入本文件（Matcher 保持 unigram DP）。
#pragma once

#include <vector>

#include "lattice_options.h"
#include "matcher.h"
#include "ngram.h"
#include "syllable.h"

namespace naive_pinyin {

// 解出最多 opts.nbest 条到达最大可达位置 m 的完整路径（含 trigram 重排），
// 分数降序、文本去重。consumed 均为 m，语义与 unigram DP 一致
// （完整消耗优先，否则最长可消耗前缀）。
std::vector<MatchCandidate> LatticeDecode(
    const Segmentation& seg,
    const std::vector<std::vector<WordEdge>>& edges,
    const Matcher& matcher, const NgramTable* bigram,
    const NgramTable* trigram, const LatticeOptions& opts);

}  // namespace naive_pinyin
