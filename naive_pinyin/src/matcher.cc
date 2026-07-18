#include "matcher.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace naive_pinyin {

namespace {

constexpr double kNegInf = -1e18;

// 一条词边：raw input [start, end) 对应词典中某个 key 的候选集。
struct WordEdge {
  int start;
  int end;
  const std::vector<DictEntry>* entries;
};

bool IsInitial(const std::string& s) {
  static const std::unordered_set<std::string> kInitials = {
      "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h",
      "j", "q", "x", "zh", "ch", "sh", "r", "z", "c", "s", "y", "w"};
  return kInitials.count(s) > 0;
}

}  // namespace

Matcher::Matcher(const Dict& dict,
                 std::vector<std::pair<std::string, std::string>> fuzzy,
                 int max_candidates, int segment_penalty,
                 const std::unordered_map<std::string, std::string>* shuangpin_map)
    : dict_(dict),
      max_candidates_(max_candidates),
      segment_penalty_(segment_penalty),
      shuangpin_map_(shuangpin_map) {
  for (auto& p : fuzzy) {
    if (IsInitial(p.first) && IsInitial(p.second)) {
      initial_pairs_.push_back(std::move(p));
    } else {
      final_pairs_.push_back(std::move(p));
    }
  }
}

std::vector<std::string> Matcher::Variants(const std::string& syllable) const {
  std::string initial, final_;
  SplitSyllable(syllable, &initial, &final_);

  std::vector<std::string> inits = {initial};
  for (const auto& p : initial_pairs_) {
    if (p.first == initial) inits.push_back(p.second);
    else if (p.second == initial) inits.push_back(p.first);
  }
  std::vector<std::string> finals = {final_};
  for (const auto& p : final_pairs_) {
    if (p.first == final_) finals.push_back(p.second);
    else if (p.second == final_) finals.push_back(p.first);
  }

  std::vector<std::string> out;
  for (const auto& i : inits) {
    for (const auto& f : finals) {
      std::string v = i + f;
      if (IsSyllable(v)) out.push_back(std::move(v));
    }
  }
  return out;
}

namespace {

// 从 pos 出发枚举词边：音节边 × 模糊变体，沿词典 trie 走。
void EnumWordEdges(const Segmentation& seg, int pos, const Dict::Node* node,
                   int depth, int max_depth, const Matcher& matcher,
                   int start, std::vector<WordEdge>* out) {
  if (!node->entries.empty()) {
    out->push_back({start, pos, &node->entries});
  }
  if (depth >= max_depth) return;
  // apostrophe 对词边透明：用户的手动分词提示不应阻断整词匹配。
  if (seg.boundary[pos]) {
    EnumWordEdges(seg, pos + 1, node, depth, max_depth, matcher, start, out);
    return;
  }
  for (const SyllableEdge& e : seg.edges_from[pos]) {
    for (const std::string& v : matcher.Variants(e.syllable)) {
      auto it = node->children.find(v);
      if (it != node->children.end()) {
        EnumWordEdges(seg, e.end, it->second.get(), depth + 1, max_depth,
                      matcher, start, out);
      }
    }
  }
}

}  // namespace

std::vector<MatchCandidate> Matcher::Match(const std::string& input) const {
  std::vector<MatchCandidate> result;
  if (input.empty() || dict_.num_entries() == 0) return result;

  Segmentation seg = (shuangpin_map_ && !shuangpin_map_->empty())
                         ? SegmentShuangpin(input, *shuangpin_map_)
                         : SegmentFullPinyin(input);
  const int n = seg.length;
  const int max_depth = dict_.max_key_length();

  // 预枚举每个起点的词边，fwd/bwd/候选生成共用。
  std::vector<std::vector<WordEdge>> edges_cache(n + 1);
  for (int i = 0; i <= n; ++i) {
    if (!seg.boundary[i]) {
      EnumWordEdges(seg, i, dict_.root(), 0, max_depth, *this, i,
                    &edges_cache[i]);
    }
  }
  auto word_edges_from = [&](int pos) -> const std::vector<WordEdge>& {
    return edges_cache[pos];
  };

  // ---- 前向 DP：fwd[i] = 到达 raw 下标 i 的最高分 ----
  std::vector<double> fwd(n + 1, kNegInf);
  fwd[0] = 0;
  for (int i = 0; i <= n; ++i) {
    if (fwd[i] == kNegInf) continue;
    if (seg.boundary[i] && i + 1 <= n) {
      fwd[i + 1] = std::max(fwd[i + 1], fwd[i]);
    }
    for (const WordEdge& e : word_edges_from(i)) {
      double s = fwd[i] + e.entries->front().score - segment_penalty_;
      fwd[e.end] = std::max(fwd[e.end], s);
    }
  }

  // 最大可达位置（完整消耗优先，否则最长前缀）。
  int m = n;
  while (m > 0 && fwd[m] == kNegInf) --m;
  if (m == 0) return result;

  // ---- 后向 DP：bwd[i] = 从 i 到 m 的最高分 ----
  std::vector<double> bwd(n + 1, kNegInf);
  std::vector<int> bwd_next(n + 1, -1);
  std::vector<std::string> bwd_word(n + 1);
  bwd[m] = 0;
  for (int i = m - 1; i >= 0; --i) {
    if (seg.boundary[i] && bwd[i + 1] > kNegInf) {
      bwd[i] = bwd[i + 1];
      bwd_next[i] = i + 1;
    }
    for (const WordEdge& e : word_edges_from(i)) {
      if (e.end > m || bwd[e.end] == kNegInf) continue;
      double s = e.entries->front().score - segment_penalty_ + bwd[e.end];
      if (s > bwd[i]) {
        bwd[i] = s;
        bwd_next[i] = e.end;
        bwd_word[i] = e.entries->front().word;
      }
    }
  }

  auto reconstruct = [&](int pos) {
    std::string text;
    while (pos < m && bwd_next[pos] >= 0) {
      text += bwd_word[pos];
      pos = bwd_next[pos];
    }
    return text;
  };

  // ---- 候选生成：枚举首词 × 最优补全 ----
  // 首词即全程(单字/单词查询)时放开到 max_candidates；
  // 否则取前 3，保证整句候选的多样性。
  static constexpr size_t kTopEntriesPerPartialEdge = 3;
  for (const WordEdge& e : word_edges_from(0)) {
    if (bwd[e.end] == kNegInf) continue;
    const std::string completion = reconstruct(e.end);
    const bool full = (e.end == m);
    size_t k = full ? std::min(static_cast<size_t>(max_candidates_),
                               e.entries->size())
                    : std::min(kTopEntriesPerPartialEdge, e.entries->size());
    for (size_t t = 0; t < k; ++t) {
      const DictEntry& entry = (*e.entries)[t];
      double total = entry.score - segment_penalty_ + bwd[e.end];
      result.push_back({entry.word + completion, m, total});
    }
  }

  // 去重（同文本取最高分）+ 降序 + 截断。
  std::sort(result.begin(), result.end(),
            [](const MatchCandidate& a, const MatchCandidate& b) {
              return a.score > b.score;
            });
  std::vector<MatchCandidate> deduped;
  std::unordered_set<std::string> seen;
  for (auto& c : result) {
    if (seen.insert(c.text).second) deduped.push_back(std::move(c));
    if (static_cast<int>(deduped.size()) >= max_candidates_) break;
  }
  return deduped;
}

}  // namespace naive_pinyin
