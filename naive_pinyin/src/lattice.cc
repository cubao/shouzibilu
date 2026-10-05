#include "lattice.h"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace naive_pinyin {

namespace {

// ---- 主切分：最少音节数路径 ----
// msime 在固定的一条音节序列上建词格（cut_one_piece_min_segments：DP 求
// 最少段数，同段数取更短首段），音节歧义在切分层解决。若让词格在全边界
// DAG 上自选切分，「同字母切出更多音节」会多拿每音节的 phrase bonus
//（de|ti|an|qi 8 音节压过 de|tian|qi 7 音节），整句质量崩坏。
// apostrophe 是零步通过（boundary[i] 为真处 i→i+1 不耗音节），既可自成
// 一步，也可被跨它的透明词边覆盖。
struct ChosenSegmentation {
  std::vector<int> step_from;             // 每步起点（raw 下标）
  std::vector<int> step_to;               // 每步终点
  std::vector<std::string> step_syllable; // 每步音节；"" = apostrophe 零步
  int m = 0;

  int syllable_count() const {
    int count = 0;
    for (const auto& s : step_syllable) {
      if (!s.empty()) ++count;
    }
    return count;
  }
  std::string signature() const {
    std::string out;
    for (const auto& s : step_syllable) {
      if (s.empty()) continue;
      if (!out.empty()) out += '|';
      out += s;
    }
    return out;
  }
  // 排序键：(音节数, 首段长度, 音节序列字典序)——[0] 即最少段主切分，
  // 与 msime cut_one_piece_min_segments 的 fan'gan 倾向一致。
  auto rank() const {
    return std::make_tuple(syllable_count(), step_to.front() - step_from.front(),
                           step_syllable);
  }
};

// 主切分：DP 求到最大可达位置 m 的最少音节数路径。
ChosenSegmentation ChooseMinSegmentation(const Segmentation& seg) {
  const int n = seg.length;
  ChosenSegmentation chosen;
  constexpr int kInf = 1 << 30;

  // 前向：reach[i] = 到 i 的最少音节数。i 升序松弛，零步先于音节边，
  // 最后一个可达位置即最大可消耗前缀 m。
  std::vector<int> reach(n + 1, kInf);
  reach[0] = 0;
  int m = 0;
  for (int i = 0; i <= n; ++i) {
    if (reach[i] == kInf) continue;
    m = i;
    if (seg.boundary[i] && i + 1 <= n && reach[i] < reach[i + 1]) {
      reach[i + 1] = reach[i];
    }
    for (const SyllableEdge& e : seg.edges_from[i]) {
      if (reach[i] + 1 < reach[e.end]) reach[e.end] = reach[i] + 1;
    }
  }
  chosen.m = m;
  if (m == 0) return chosen;

  // 后向：bwd[i] = 从 i 到 m 的最少音节数。
  std::vector<int> bwd(m + 1, kInf);
  bwd[m] = 0;
  for (int i = m - 1; i >= 0; --i) {
    if (reach[i] == kInf) continue;
    if (seg.boundary[i] && bwd[i + 1] < kInf) bwd[i] = bwd[i + 1];
    for (const SyllableEdge& e : seg.edges_from[i]) {
      if (e.end > m || bwd[e.end] == kInf) continue;
      bwd[i] = std::min(bwd[i], 1 + bwd[e.end]);
    }
  }

  // 前向重建：最少段数的后继里取更短首段，再按音节字典序；
  // 同分时优先音节边、零步兜底。
  int pos = 0;
  while (pos < m) {
    const SyllableEdge* best = nullptr;
    for (const SyllableEdge& e : seg.edges_from[pos]) {
      if (e.end > m || bwd[e.end] == kInf) continue;
      if (1 + bwd[e.end] != bwd[pos]) continue;
      if (best == nullptr || e.end - pos < best->end - pos ||
          (e.end - pos == best->end - pos && e.syllable < best->syllable)) {
        best = &e;
      }
    }
    if (best != nullptr) {
      chosen.step_from.push_back(pos);
      chosen.step_to.push_back(best->end);
      chosen.step_syllable.push_back(best->syllable);
      pos = best->end;
      continue;
    }
    if (seg.boundary[pos] && bwd[pos + 1] == bwd[pos]) {
      chosen.step_from.push_back(pos);
      chosen.step_to.push_back(pos + 1);
      chosen.step_syllable.emplace_back("");
      pos += 1;
      continue;
    }
    break;  // 不应发生（bwd[0] 有限保证有后继）
  }
  if (pos < m) chosen.m = pos;  // 保守：截到实际重建到的位置
  return chosen;
}

// 枚举完整切分（音节边 + apostrophe 零步、都正好到达 m 的路径），
// 按 rank() 排序，同音节序列去重。上限 limit 防组合爆炸。
std::vector<ChosenSegmentation> EnumerateSegmentations(const Segmentation& seg,
                                                       int m, int limit) {
  std::vector<ChosenSegmentation> out;
  if (m <= 0) return out;
  std::vector<int> from, to;
  std::vector<std::string> syl;
  auto dfs = [&](auto&& self, int pos) -> void {
    if (static_cast<int>(out.size()) >= limit) return;
    if (pos == m) {
      ChosenSegmentation c;
      c.m = m;
      c.step_from = from;
      c.step_to = to;
      c.step_syllable = syl;
      out.push_back(std::move(c));
      return;
    }
    if (seg.boundary[pos] && pos + 1 <= m) {
      from.push_back(pos);
      to.push_back(pos + 1);
      syl.emplace_back("");
      self(self, pos + 1);
      from.pop_back();
      to.pop_back();
      syl.pop_back();
    }
    // 最长边优先（对齐 msime enumerate_complete_segmentations 的
    // fang'an 在 fan'gan 之前的顺序）：深度优先回溯先变尾位选择，
    // 尾部音节歧义（如 fang|an vs fan|gan）能在有限条数内被枚举到。
    for (auto it = seg.edges_from[pos].rbegin();
         it != seg.edges_from[pos].rend(); ++it) {
      const SyllableEdge& e = *it;
      if (e.end > m) continue;
      if (static_cast<int>(out.size()) >= limit) return;
      from.push_back(pos);
      to.push_back(e.end);
      syl.push_back(e.syllable);
      self(self, e.end);
      from.pop_back();
      to.pop_back();
      syl.pop_back();
    }
  };
  dfs(dfs, 0);

  std::stable_sort(out.begin(), out.end(),
                   [](const ChosenSegmentation& a,
                      const ChosenSegmentation& b) {
                     return a.rank() < b.rank();
                   });
  std::vector<ChosenSegmentation> deduped;
  std::unordered_set<std::string> seen;
  for (auto& c : out) {
    if (seen.insert(c.signature()).second) deduped.push_back(std::move(c));
  }
  return deduped;
}

// 把词边过滤到与给定切分对齐：start 是某步的起点，沿切分逐步走到
// end，途经的非零步音节串逐个等于词边 key 的音节。
std::vector<std::vector<WordEdge>> AlignEdgesToSegmentation(
    const std::vector<std::vector<WordEdge>>& edges,
    const ChosenSegmentation& chosen) {
  std::unordered_map<int, int> pos_index;
  for (int k = 0; k < static_cast<int>(chosen.step_from.size()); ++k) {
    pos_index.emplace(chosen.step_from[k], k);  // 同起点取首步
  }
  auto syllables_of = [](const std::string& key) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= key.size()) {
      size_t space = key.find(' ', start);
      if (space == std::string::npos) space = key.size();
      out.emplace_back(key.substr(start, space - start));
      start = space + 1;
    }
    return out;
  };

  std::vector<std::vector<WordEdge>> aligned(edges.size());
  for (size_t pos = 0; pos < edges.size(); ++pos) {
    auto from = pos_index.find(static_cast<int>(pos));
    if (from == pos_index.end()) continue;
    for (const WordEdge& e : edges[pos]) {
      std::vector<std::string> walked;
      int cursor = from->second;
      bool ok = false;
      for (int guard = 0; guard <= static_cast<int>(chosen.step_from.size());
           ++guard) {
        if (!chosen.step_syllable[cursor].empty()) {
          walked.push_back(chosen.step_syllable[cursor]);
        }
        if (chosen.step_to[cursor] == e.end) {
          ok = true;
          break;
        }
        if (chosen.step_to[cursor] > e.end) break;
        auto next = pos_index.find(chosen.step_to[cursor]);
        if (next == pos_index.end()) break;
        cursor = next->second;
      }
      if (ok && walked == syllables_of(e.key)) {
        aligned[pos].push_back(e);
      }
    }
  }
  return aligned;
}

struct Hyp {
  double score = 0;
  int prev = -1;               // 前驱假设在 arena 中的下标；-1 = 起点
  const WordEdge* edge = nullptr;   // 产生本假设的词边；nullptr = epsilon
  const DictEntry* entry = nullptr; // 词边 entries 中的词条
  std::string last_word;       // 供下一转移的 bigram 前词（epsilon 不更新）
};

// 词格边的对数域基础分（decode.rs edge_log_prob）：
// 单字 = char_span×(s/1000) + char_floor（msime: ln(w)-ln(1e6)）；
// 多字 = phrase_bonus×音节数 + phrase_span×(s/1000)（msime: ln(w)+20n）。
double EdgeLogProb(const WordEdge& e, const DictEntry& entry,
                   const LatticeOptions& opts) {
  const int n_syl =
      1 + static_cast<int>(std::count(e.key.begin(), e.key.end(), ' '));
  if (n_syl <= 1) {
    return opts.unigram_char_span * (entry.score / 1000.0) +
           opts.unigram_char_floor;
  }
  return opts.phrase_bonus * n_syl +
         opts.unigram_phrase_span * (entry.score / 1000.0);
}

double BigramValue(const NgramTable& table, const std::string& prev,
                   const std::string& next) {
  if (prev.empty()) {
    const std::string start = kNgramSentenceStart;
    const std::string words[2] = {start, next};
    return table.Increment(words, 2);
  }
  const std::string words[2] = {prev, next};
  return table.Increment(words, 2);
}

double TrigramValue(const NgramTable& table, const std::string& before,
                    const std::string& prev, const std::string& next) {
  if (before.empty()) {
    const std::string start = kNgramSentenceStart;
    const std::string words[3] = {start, prev, next};
    return table.Increment(words, 3);
  }
  const std::string words[3] = {before, prev, next};
  return table.Increment(words, 3);
}

// trigram 对 n-best 重排（decode.rs rescore_with_trigram）：
// path += trigram_weight × Σ trigram(w[i-1], w[i], w[i+1])，句首前词补
// kNgramSentenceStart。单词路径无三元组，分不变。
double TrigramRescore(const NgramTable& trigram, const MatchCandidate& path,
                      double weight) {
  if (path.segments.size() < 2) return path.score;
  double sum = 0;
  for (size_t i = 0; i + 1 < path.segments.size(); ++i) {
    const std::string& before =
        (i == 0) ? std::string() : path.segments[i - 1].second;
    sum += TrigramValue(trigram, before, path.segments[i].second,
                        path.segments[i + 1].second);
  }
  return path.score + weight * sum;
}

// 在一条给定切分（edges 已与切分对齐）上做 beam 解码，返回文本去重、
// trigram 重排后的路径（分数降序，最多 max(opts.nbest,12) 条供编排取舍）。
// 移植自 msime crates/engine/src/lattice/decode.rs 的 decode_graph。
std::vector<MatchCandidate> DecodeOnSteps(
    const Segmentation& seg,
    const std::vector<std::vector<WordEdge>>& aligned,
    const Matcher& matcher, const NgramTable* bigram,
    const NgramTable* trigram, const LatticeOptions& opts) {
  const int n = seg.length;
  std::vector<MatchCandidate> result;
  if (n <= 0) return result;

  // 假设驻留在 arena（下标永不失效），每列 beam 存 arena 下标；
  // 剪枝只重排列里的下标顺序，回链不受影响。
  // edge/entry 指向 aligned 与词典 trie，本次查询期内稳定。
  std::deque<Hyp> arena;
  std::vector<std::vector<int>> beams(n + 1);
  arena.emplace_back();  // 0 号 = 起点
  beams[0].push_back(0);

  for (int col = 0; col < n; ++col) {
    auto& beam = beams[col];
    if (beam.empty()) continue;
    if (static_cast<int>(beam.size()) > opts.beam) {
      std::stable_sort(beam.begin(), beam.end(),
                       [&](int a, int b) {
                         return arena[a].score > arena[b].score;
                       });
      beam.resize(opts.beam);
    }

    // apostrophe 零步通过：等分 epsilon 转移，前词不变。
    if (seg.boundary[col]) {
      for (int h_idx : beam) {
        Hyp h = arena[h_idx];
        h.prev = h_idx;
        h.edge = nullptr;
        h.entry = nullptr;
        arena.push_back(std::move(h));
        beams[col + 1].push_back(static_cast<int>(arena.size()) - 1);
      }
    }

    for (int h_idx : beam) {
      const Hyp& h = arena[h_idx];
      const auto& word_edges = aligned[col];
      for (const WordEdge& e : word_edges) {
        const int take = std::min<int>(static_cast<int>(e.entries->size()),
                                       opts.max_entries_per_edge);
        for (int t = 0; t < take; ++t) {
          const DictEntry& entry = (*e.entries)[t];
          double s = h.score + EdgeLogProb(e, entry, opts);
          s += opts.bigram_weight *
               BigramValue(*bigram, h.last_word, entry.word);
          const int boost = matcher.UserBoost(e, entry);
          if (boost > 0) {
            s += opts.personal_weight *
                 std::min(opts.personal_max, 0.005 * boost);
          }
          Hyp nh;
          nh.score = s;
          nh.prev = h_idx;
          nh.edge = &e;
          nh.entry = &entry;
          nh.last_word = entry.word;
          arena.push_back(std::move(nh));
          beams[e.end].push_back(static_cast<int>(arena.size()) - 1);
        }
      }
    }
  }

  // 终点列 m：完整消耗优先，否则最长可消耗前缀（与 unigram DP 一致）。
  int m = -1;
  for (int c = n; c >= 1; --c) {
    if (!beams[c].empty()) {
      m = c;
      break;
    }
  }
  if (m <= 0) return result;

  auto& final_beam = beams[m];
  std::stable_sort(final_beam.begin(), final_beam.end(),
                   [&](int a, int b) { return arena[a].score > arena[b].score; });

  // 提取文本去重的路径，多取一些给 trigram 重排留余地
  //（decode.rs：解出比展示更多的路径）。
  const size_t kExtract = std::max<size_t>(opts.nbest, 12);
  std::vector<MatchCandidate> paths;
  std::unordered_set<std::string> seen;
  for (int h_idx : final_beam) {
    if (paths.size() >= kExtract) break;
    MatchCandidate cand;
    cand.consumed = m;
    cand.score = arena[h_idx].score;  // trigram 重排前的基础分
    std::vector<std::pair<std::string, std::string>> reversed;
    for (int cur = h_idx; cur > 0;) {
      const Hyp& h = arena[cur];
      if (h.edge != nullptr) {
        reversed.emplace_back(h.edge->key, h.entry->word);
      }
      cur = h.prev;
    }
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
      cand.segments.push_back(std::move(*it));
    }
    for (const auto& seg_pair : cand.segments) {
      cand.text += seg_pair.second;
    }
    if (seen.insert(cand.text).second) paths.push_back(std::move(cand));
  }

  if (trigram != nullptr && !trigram->empty()) {
    for (auto& p : paths) {
      p.score = TrigramRescore(*trigram, p, opts.trigram_weight);
    }
    std::stable_sort(paths.begin(), paths.end(),
                     [](const MatchCandidate& a, const MatchCandidate& b) {
                       return a.score > b.score;
                     });
  }
  return paths;
}

}  // namespace

std::vector<MatchCandidate> LatticeDecode(
    const Segmentation& seg,
    const std::vector<std::vector<WordEdge>>& edges,
    const Matcher& matcher, const NgramTable* bigram,
    const NgramTable* trigram, const LatticeOptions& opts) {
  std::vector<MatchCandidate> result;
  if (seg.length <= 0 || bigram == nullptr || bigram->empty()) return result;

  // 主切分（最少段）优先；备选切分补位——最少段切分会把
  // fangan 定成 fan|gan，方案（fang|an）只活在备选里
  //（msime golden s-024 记录了同样的缺口，这里用备选整句补上）。
  std::vector<ChosenSegmentation> segmentations =
      EnumerateSegmentations(seg, ChooseMinSegmentation(seg).m, 32);
  if (segmentations.empty()) return result;

  struct Ranked {
    MatchCandidate cand;
    int priority;  // 0 = 主切分
  };
  std::vector<Ranked> ranked;
  int alternates_used = 0;
  for (size_t si = 0; si < segmentations.size(); ++si) {
    const bool primary = (si == 0);
    if (!primary) {
      if (alternates_used >= opts.max_alternates) break;
      ++alternates_used;
    }
    std::vector<std::vector<WordEdge>> aligned =
        AlignEdgesToSegmentation(edges, segmentations[si]);
    bool any = false;
    for (const auto& e : aligned) {
      if (!e.empty()) {
        any = true;
        break;
      }
    }
    if (!any) continue;
    std::vector<MatchCandidate> paths = DecodeOnSteps(
        seg, aligned, matcher, bigram, trigram, opts);
    const size_t keep = primary
        ? paths.size()
        : std::min<size_t>(paths.size(), 2);  // 每个备选切分只递补 2 条
    for (size_t k = 0; k < keep; ++k) {
      ranked.push_back({std::move(paths[k]), primary ? 0 : 1});
    }
  }
  if (ranked.empty()) return result;

  // 主切分让出至多 2 个槽位给备选整句（fangan 案例里方案必须可见），
  // 无备选路径时主切分占满 nbest。
  bool have_alternates = false;
  for (const auto& r : ranked) {
    if (r.priority != 0) {
      have_alternates = true;
      break;
    }
  }
  const size_t primary_take =
      have_alternates
          ? std::max<size_t>(1, static_cast<size_t>(opts.nbest) - 2)
          : static_cast<size_t>(opts.nbest);

  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked& a, const Ranked& b) {
                     if (a.priority != b.priority) return a.priority < b.priority;
                     return a.cand.score > b.cand.score;
                   });
  std::unordered_set<std::string> seen;
  std::vector<char> taken(ranked.size(), 0);
  // 第一遍：主切分按分取满份额；第二遍：备选先递补（fang|an 的方案
  // 必须排在主切分剩余变体之前），最后主切分填满。
  size_t primary_taken = 0;
  for (size_t i = 0; i < ranked.size(); ++i) {
    if (ranked[i].priority != 0) continue;
    if (primary_taken >= primary_take) break;
    if (static_cast<int>(result.size()) >= opts.nbest) break;
    if (seen.insert(ranked[i].cand.text).second) {
      result.push_back(std::move(ranked[i].cand));
      taken[i] = 1;
      ++primary_taken;
    }
  }
  for (int pass = 1; pass >= 0; --pass) {  // 1 = 备选先
    for (size_t i = 0; i < ranked.size(); ++i) {
      if (taken[i]) continue;
      if ((ranked[i].priority != 0) != (pass == 1)) continue;
      if (static_cast<int>(result.size()) >= opts.nbest) break;
      if (seen.insert(ranked[i].cand.text).second) {
        result.push_back(std::move(ranked[i].cand));
        taken[i] = 1;
      }
    }
  }
  return result;
}

}  // namespace naive_pinyin
