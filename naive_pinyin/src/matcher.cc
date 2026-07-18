#include "matcher.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace naive_pinyin {

namespace {

constexpr double kNegInf = -1e18;

// 一条词边：raw input [start, end) 对应词典中某个 key 的候选集。
// key 是词典侧的音节序列（模糊变体展开后的实际命中路径）。
struct WordEdge {
  int start;
  int end;
  const std::vector<DictEntry>* entries;
  std::string key;
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
                 const std::unordered_map<std::string, std::string>* shuangpin_map,
                 const UserFreq* user_freq)
    : dict_(dict),
      max_candidates_(max_candidates),
      segment_penalty_(segment_penalty),
      shuangpin_map_(shuangpin_map),
      user_freq_(user_freq) {
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
// key_path 累积词典侧音节路径（供调频叠加层查询）。
void EnumWordEdges(const Segmentation& seg, int pos, const Dict::Node* node,
                   int depth, int max_depth, const Matcher& matcher,
                   int start, std::string* key_path,
                   std::vector<WordEdge>* out) {
  if (!node->entries.empty()) {
    out->push_back({start, pos, &node->entries, *key_path});
  }
  if (depth >= max_depth) return;
  // apostrophe 对词边透明：用户的手动分词提示不应阻断整词匹配。
  if (seg.boundary[pos]) {
    EnumWordEdges(seg, pos + 1, node, depth, max_depth, matcher, start,
                  key_path, out);
    return;
  }
  const size_t base_len = key_path->size();
  for (const SyllableEdge& e : seg.edges_from[pos]) {
    for (const std::string& v : matcher.Variants(e.syllable)) {
      auto it = node->children.find(v);
      if (it != node->children.end()) {
        if (!key_path->empty()) key_path->push_back(' ');
        *key_path += v;
        EnumWordEdges(seg, e.end, it->second.get(), depth + 1, max_depth,
                      matcher, start, key_path, out);
        key_path->resize(base_len);
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
      std::string key_path;
      EnumWordEdges(seg, i, dict_.root(), 0, max_depth, *this, i, &key_path,
                    &edges_cache[i]);
    }
  }
  auto word_edges_from = [&](int pos) -> const std::vector<WordEdge>& {
    return edges_cache[pos];
  };

  // 词条有效分 = 静态分 + 用户调频加分。
  auto effective = [&](const WordEdge& e, const DictEntry& entry) {
    int boost = user_freq_ ? user_freq_->Boost(e.key, entry.word) : 0;
    return entry.score + boost;
  };
  // 词边的最优词条（有效分最高）。
  auto best_entry = [&](const WordEdge& e) -> const DictEntry* {
    const DictEntry* best = &e.entries->front();
    int best_score = effective(e, *best);
    for (const auto& entry : *e.entries) {
      int s = effective(e, entry);
      if (s > best_score) {
        best = &entry;
        best_score = s;
      }
    }
    return best;
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
      double s = fwd[i] + effective(e, *best_entry(e)) - segment_penalty_;
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
  std::vector<std::string> bwd_key(n + 1);
  bwd[m] = 0;
  for (int i = m - 1; i >= 0; --i) {
    if (seg.boundary[i] && bwd[i + 1] > kNegInf) {
      bwd[i] = bwd[i + 1];
      bwd_next[i] = i + 1;
    }
    for (const WordEdge& e : word_edges_from(i)) {
      if (e.end > m || bwd[e.end] == kNegInf) continue;
      const DictEntry* best = best_entry(e);
      double s = effective(e, *best) - segment_penalty_ + bwd[e.end];
      if (s > bwd[i]) {
        bwd[i] = s;
        bwd_next[i] = e.end;
        bwd_word[i] = best->word;
        bwd_key[i] = e.key;
      }
    }
  }

  auto reconstruct = [&](int pos, std::string* text,
                         std::vector<std::pair<std::string, std::string>>* segs) {
    while (pos < m && bwd_next[pos] >= 0) {
      if (!bwd_word[pos].empty()) {
        *text += bwd_word[pos];
        segs->emplace_back(bwd_key[pos], bwd_word[pos]);
      }
      pos = bwd_next[pos];
    }
  };

  // ---- 候选生成：枚举首词 × 最优补全 ----
  // 首词即全程(单字/单词查询)时放开到 max_candidates；
  // 否则取前 3，保证整句候选的多样性。
  static constexpr size_t kTopEntriesPerPartialEdge = 3;
  for (const WordEdge& e : word_edges_from(0)) {
    if (bwd[e.end] == kNegInf) continue;
    // 首词词条按有效分降序。
    std::vector<const DictEntry*> sorted;
    sorted.reserve(e.entries->size());
    for (const auto& entry : *e.entries) sorted.push_back(&entry);
    std::sort(sorted.begin(), sorted.end(),
              [&](const DictEntry* a, const DictEntry* b) {
                return effective(e, *a) > effective(e, *b);
              });

    const bool full = (e.end == m);
    size_t k = full ? std::min(static_cast<size_t>(max_candidates_),
                               sorted.size())
                    : std::min(kTopEntriesPerPartialEdge, sorted.size());
    for (size_t t = 0; t < k; ++t) {
      const DictEntry& entry = *sorted[t];
      double total = effective(e, entry) - segment_penalty_ + bwd[e.end];
      MatchCandidate cand;
      cand.consumed = m;
      cand.score = total;
      cand.segments.emplace_back(e.key, entry.word);
      reconstruct(e.end, &cand.text, &cand.segments);
      cand.text = entry.word + cand.text;
      result.push_back(std::move(cand));
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
