// 候选生成：音节 DAG × 词典 trie → 候选。
// 无语言模型时走 unigram DP（历史行为，逐字节保留）；
// 加载 bigram 表后走词格 beam 解码（lattice.h）+ 候选编排。
#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.h"
#include "dict.h"
#include "syllable.h"
#include "user_freq.h"

namespace naive_pinyin {

class NgramTable;
struct LatticeOptions;

struct MatchCandidate {
  std::string text;
  int consumed = 0;   // 消耗掉的 raw input 字符数（含 apostrophe）
  double score = 0;
  // 分段信息：(拼音key, 词)，供提交调频/自造词检测。
  std::vector<std::pair<std::string, std::string>> segments;
};

// 一条词边：raw input [start, end) 对应词典中某个 key 的候选集。
// key 是词典侧的音节序列（模糊变体展开后的实际命中路径）。
// entries 指向 trie 节点，按分数降序。
struct WordEdge {
  int start;
  int end;
  const std::vector<DictEntry>* entries;
  std::string key;
};

class Matcher {
 public:
  // segment_penalty: 每多一个分段扣的分。越大越偏好长词/少分段。
  // shuangpin_map 非空时按双拼切分（2 字母一码查表），否则全拼。
  // user_freq 非空时应用动态调频加分。
  Matcher(const Dict& dict, std::vector<std::pair<std::string, std::string>> fuzzy,
          int max_candidates, int segment_penalty,
          const std::unordered_map<std::string, std::string>* shuangpin_map = nullptr,
          const UserFreq* user_freq = nullptr);

  std::vector<MatchCandidate> Match(const std::string& input) const;

  // 生成音节的模糊音变体（含原音节）。
  std::vector<std::string> Variants(const std::string& syllable) const;

  // 启用词格解码：bigram/trigram 表与参数均由 Engine 持有，生命周期
  // 须覆盖 Match 调用。bigram 为空或 opts->enabled 为 false 时保持
  // unigram DP 行为。
  void SetLm(const NgramTable* bigram, const NgramTable* trigram,
             const LatticeOptions* opts) {
    lm_bigram_ = bigram;
    lm_trigram_ = trigram;
    lattice_opts_ = opts;
  }

  // 词条有效分 = 静态分 + 用户调频加分（score 域，供词格与编排共用）。
  int Effective(const WordEdge& e, const DictEntry& entry) const {
    return entry.score + UserBoost(e, entry);
  }

  // 该词条的用户调频加分（无叠加层时为 0）。
  int UserBoost(const WordEdge& e, const DictEntry& entry) const {
    return user_freq_ ? user_freq_->Boost(e.key, entry.word) : 0;
  }

  const Dict& dict() const { return dict_; }

  // 预枚举每个起点的词边（模糊音展开 × trie），前向/后向/词格/编排共用。
  std::vector<std::vector<WordEdge>> EnumEdges(const Segmentation& seg) const;

 private:
  const Dict& dict_;
  int max_candidates_;
  int segment_penalty_;
  const std::unordered_map<std::string, std::string>* shuangpin_map_;
  const UserFreq* user_freq_;

  const NgramTable* lm_bigram_ = nullptr;
  const NgramTable* lm_trigram_ = nullptr;
  const LatticeOptions* lattice_opts_ = nullptr;

  // 模糊音规则：声母对（如 z<->zh）与韵母对（如 in<->ing）。
  std::vector<std::pair<std::string, std::string>> initial_pairs_;
  std::vector<std::pair<std::string, std::string>> final_pairs_;
};

}  // namespace naive_pinyin
