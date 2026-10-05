// 词格解码 + 候选编排的行为测试。
// 用小词典 + 测试内构建的 MSNG 表验证：bigram 翻转排序、trigram 重排、
// 精确行优先编排、部分消耗、apostrophe、用户调频加成、LM 关闭回退。
#include "../naive_pinyin/src/config.h"
#include "../naive_pinyin/src/engine.h"
#include "../naive_pinyin/src/matcher.h"
#include "../naive_pinyin/src/ngram.h"

#include <cstring>
#include <string>
#include <vector>

#include "msng_builder.h"
#include "test_framework.h"

using namespace msng_test_util;
using naive_pinyin::Config;
using naive_pinyin::Dict;
using naive_pinyin::EngineImpl;
using naive_pinyin::LatticeOptions;
using naive_pinyin::MatchCandidate;
using naive_pinyin::Matcher;
using naive_pinyin::NgramTable;
using naive_pinyin::UserFreq;

namespace {

Dict& GetDict() {
  static Dict dict = [] {
    // 注意：shi jie 故意不设词条，测纯词格排序（见 bigram_flips_ranking）。
    const char* text = R"(
ni	你:920,尼:800
hao	好:880,号:850
shi	世:900,是:950,十:880
jie	界:870,节:860
ren	人:950
ni hao	你好:758
hao shi	好事:700
ni hao shi jie	你好世界:782
shi jie	世界:782,使节:566
)";
    Dict d;
    d.Load(text, std::strlen(text));
    return d;
  }();
  return dict;
}

Dict& GetNoExactDict() {
  static Dict dict = [] {
    const char* text = R"(
shi	世:900,是:950,十:880
jie	界:870,节:860
)";
    Dict d;
    d.Load(text, std::strlen(text));
    return d;
  }();
  return dict;
}

std::vector<MatchCandidate> MatchLm(
    Dict& dict, const std::string& input, const std::string& bigram_data,
    const std::string& trigram_data = "", const LatticeOptions* opts = nullptr,
    UserFreq* user_freq = nullptr) {
  static NgramTable bigram, trigram;
  static LatticeOptions options;
  ASSERT(bigram.Load(bigram_data.data(), bigram_data.size()));
  if (!trigram_data.empty()) {
    ASSERT(trigram.Load(trigram_data.data(), trigram_data.size()));
  } else {
    ASSERT(trigram.Load("", 0) || trigram.empty());
  }
  options = opts ? *opts : LatticeOptions{};
  Matcher matcher(dict, {}, 10, 1100, nullptr, user_freq);
  matcher.SetLm(&bigram, trigram.empty() ? nullptr : &trigram, &options);
  return matcher.Match(input);
}

std::vector<std::string> Texts(const std::vector<MatchCandidate>& cands) {
  std::vector<std::string> out;
  for (const auto& c : cands) out.push_back(c.text);
  return out;
}

bool Contains(const std::vector<MatchCandidate>& cands,
              const std::string& text) {
  for (const auto& c : cands) {
    if (c.text == text) return true;
  }
  return false;
}

}  // namespace

TEST(lattice, exact_dict_rows_lead) {
  // 全键精确行（世界/使节）领先；词格整句（如 你好世界 不覆盖此输入）
  // 不得插到它们前面（msime merge.rs 语义）。
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "shijie", bigram);
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "世界");
  ASSERT_EQ(Texts(cands)[1], "使节");
  ASSERT_EQ(cands[0].consumed, 6);
}

TEST(lattice, bigram_flips_ranking_without_exact_rows) {
  // 无精确行时词格决定顺序：无 LM 的贪心链是 是(950)+界；
  // bigram(世,界)=+3 让 世+界 链翻到第一。
  Dict& dict = GetNoExactDict();
  auto plain = MatchLm(dict, "shijie", BuildTable({}));
  ASSERT_EQ(plain[0].text, "是界");
  auto with_lm = MatchLm(dict, "shijie", BuildTable({{HashPair("世", "界"), 3.0f}}));
  ASSERT_EQ(with_lm[0].text, "世界");
}

TEST(lattice, sentence_path_beats_greedy_chain) {
  // 精确行（4 音节词 你好世界）领先，consumed 覆盖全部输入。
  const std::string bigram =
      BuildTable({{HashPair("你好", "世界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "nihaoshijie", bigram);
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "你好世界");
  ASSERT_EQ(cands[0].consumed, 11);
  ASSERT_EQ(cands[0].segments.size(), 1u);  // 精确行是单个词条
}

TEST(lattice, bigram_promotes_collocation_sentence) {
  // 无精确行的 3 音节输入：唯一首词边 好事(700)，词格整句 好事+界
  // （bigram(好事,界)=+3）应成为首选 —— 无 LM 时此输入出不了这条多词路径
  // 之外的词典词，词格让「词 + 单字」的衔接成为候选。
  Dict dict;
  const char* text = "shi\t世:900,是:950\njie\t界:870,节:860\n";
  ASSERT(dict.Load(text, std::strlen(text)));
  dict.AddEntry("hao shi", "好事", 700);
  const std::string bigram = BuildTable({{HashPair("好事", "界"), 3.0f}});
  auto cands = MatchLm(dict, "haoshijie", bigram);
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "好事界");
}

TEST(lattice, trigram_rescore_flips_order) {
  // 3 音节纯字链：bigram 平局（世/是 → 界 同 +1），trigram(世,界,节)=+3
  // 把 世+界+节 翻到重复词路径 是+界+界（bigram(是,界)=+1 使其无 trigram
  // 时第一）之前。
  Dict& dict = GetNoExactDict();
  const std::string bigram =
      BuildTable({{HashPair("世", "界"), 1.0f}, {HashPair("是", "界"), 1.0f},
                  {HashPair("界", "节"), 0.0f}});
  auto no_tri = MatchLm(dict, "shijiejie", bigram);
  ASSERT_EQ(no_tri[0].text, "是界界");
  const std::string tri =
      BuildTable({{HashTriple("世", "界", "节"), 3.0f}});
  auto with_tri = MatchLm(dict, "shijiejie", bigram, tri);
  ASSERT_EQ(with_tri[0].text, "世界节");
}

TEST(lattice, partial_consumption_on_garbage_tail) {
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "shijiex", bigram);
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "世界");
  ASSERT_EQ(cands[0].consumed, 6);
}

TEST(lattice, apostrophe_forces_boundary) {
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "shi'jie", bigram);
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "世界");
  ASSERT_EQ(cands[0].consumed, 7);  // raw 长度含 apostrophe
}

TEST(lattice, user_boost_lifts_exact_row) {
  // 精确行按有效分（静态 + boost）排序：使节(566+350) 翻到 世界(782) 前。
  UserFreq user_freq;
  user_freq.Commit("shi jie", "使节");
  user_freq.Commit("shi jie", "使节");
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "shijie", bigram, "", nullptr, &user_freq);
  ASSERT_EQ(cands[0].text, "使节");
}

TEST(lattice, disabled_options_keep_legacy_behavior) {
  LatticeOptions opts;
  opts.enabled = false;
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  auto cands = MatchLm(GetDict(), "shijie", bigram, "", &opts);
  // legacy unigram DP：世界(782) 仍第一，但顺序完全由 score 域决定，
  // 且词格专属的列不出现 —— 与未加载 LM 一致。
  ASSERT_EQ(cands[0].text, "世界");
}

TEST(lattice, respects_max_candidates) {
  static NgramTable bigram;
  const std::string data = BuildTable({{HashPair("世", "界"), 3.0f}});
  ASSERT(bigram.Load(data.data(), data.size()));
  static LatticeOptions options;  // nbest=6
  Matcher matcher(GetDict(), {}, 2, 1100);
  matcher.SetLm(&bigram, nullptr, &options);
  auto cands = matcher.Match("nihaoshijie");
  ASSERT(cands.size() <= 2u);
}

TEST(lattice, engine_level_query_with_lm) {
  EngineImpl engine(Config::FromJson("{}"));
  const char* dict_text = "shi\t世:900,是:950\njie\t界:870\nshi jie\t世界:782\n";
  ASSERT(engine.LoadDict(dict_text, std::strlen(dict_text)));
  const std::string bigram = BuildTable({{HashPair("世", "界"), 3.0f}});
  ASSERT(engine.LoadLm(bigram.data(), bigram.size()));
  // JSON 协议不变：candidates[].{text,consumed,score,segments}。
  const std::string json = engine.Query("shijie");
  ASSERT(json.find("\"世界\"") != std::string::npos);
  ASSERT(json.find("\"segments\"") != std::string::npos);
  ASSERT(json.find("\"consumed\":6") != std::string::npos);
}

TEST(lattice, lattice_options_parse_from_config) {
  Config cfg = Config::FromJson(
      R"({"lattice": {"enabled": true, "beam": 16, "nbest": 4,
                       "bigram_weight": 1.5}})");
  ASSERT(cfg.lattice.enabled);
  ASSERT_EQ(cfg.lattice.beam, 16);
  ASSERT_EQ(cfg.lattice.nbest, 4);
  ASSERT(cfg.lattice.bigram_weight == 1.5);
  Config off = Config::FromJson(R"({"lattice": false})");
  ASSERT(!off.lattice.enabled);
}
