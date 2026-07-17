// DP 整句匹配：音节 DAG × 词典 trie → 候选
#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "dict.h"
#include "syllable.h"

namespace naive_pinyin {

struct MatchCandidate {
  std::string text;
  int consumed = 0;   // 消耗掉的 raw input 字符数（含 apostrophe）
  double score = 0;
};

class Matcher {
 public:
  // segment_penalty: 每多一个分段扣的分。越大越偏好长词/少分段。
  Matcher(const Dict& dict, std::vector<std::pair<std::string, std::string>> fuzzy,
          int max_candidates, int segment_penalty);

  std::vector<MatchCandidate> Match(const std::string& input) const;

  // 生成音节的模糊音变体（含原音节）。
  std::vector<std::string> Variants(const std::string& syllable) const;

 private:
  const Dict& dict_;
  int max_candidates_;
  int segment_penalty_;

  // 模糊音规则：声母对（如 z<->zh）与韵母对（如 in<->ing）。
  std::vector<std::pair<std::string, std::string>> initial_pairs_;
  std::vector<std::pair<std::string, std::string>> final_pairs_;
};

}  // namespace naive_pinyin
