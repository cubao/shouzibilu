#include "../naive_pinyin/src/matcher.h"

#include <cstring>

#include "test_framework.h"

using naive_pinyin::Dict;
using naive_pinyin::MatchCandidate;
using naive_pinyin::Matcher;

namespace {

const char* kTestDict = R"(
ni	你:920,尼:800,呢:780
hao	好:880,号:850
ni hao	你好:758
shi	世:900,是:950,十:880
jie	界:870,节:860
shi jie	世界:782,使节:566
xian	先:900,现:890
xi	西:880,希:850
an	安:870,按:850
xi an	西安:700
zhi	只:900,知:880
zi	子:850,自:860
zhong	中:950
hua	华:900
ren	人:950
min	民:920
gong	共:880
he	和:900
guo	国:930
zhong hua ren min gong he guo	中华人民共和国:782
)";

Dict& GetDict() {
  static Dict dict = [] {
    Dict d;
    d.Load(kTestDict, std::strlen(kTestDict));
    return d;
  }();
  return dict;
}

Matcher MakeMatcher(std::vector<std::pair<std::string, std::string>> fuzzy = {},
                    int max_candidates = 10, int penalty = 1100) {
  return Matcher(GetDict(), std::move(fuzzy), max_candidates, penalty);
}

}  // namespace

TEST(matcher, single_word_beats_chars) {
  auto cands = MakeMatcher().Match("nihao");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "你好");
  ASSERT_EQ(cands[0].consumed, 5);
}

TEST(matcher, sentence_best_path) {
  auto cands = MakeMatcher().Match("nihaoshijie");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "你好世界");
  ASSERT_EQ(cands[0].consumed, 11);
}

TEST(matcher, long_word_beats_char_chain) {
  auto cands = MakeMatcher().Match("zhonghuarenmingongheguo");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "中华人民共和国");
}

TEST(matcher, single_char_beats_rare_word) {
  // "xian" 单独输入: 先(900) 应胜过 西安(700)
  auto cands = MakeMatcher().Match("xian");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "先");
  // 但 西安 应出现在候选里
  bool found = false;
  for (const auto& c : cands) found |= (c.text == "西安");
  ASSERT(found);
}

TEST(matcher, apostrophe_forces_boundary) {
  auto cands = MakeMatcher().Match("xi'an");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "西安");
  ASSERT_EQ(cands[0].consumed, 5);
}

TEST(matcher, fuzzy_initial_expands_lookup) {
  // 无模糊音: "zi" 查不到 只(zhi)
  auto plain = MakeMatcher().Match("zi");
  for (const auto& c : plain) ASSERT(c.text != "只");
  // 有 z/zh 模糊音: 只 出现
  auto fuzzy = MakeMatcher({{"z", "zh"}}).Match("zi");
  bool found = false;
  for (const auto& c : fuzzy) found |= (c.text == "只");
  ASSERT(found);
}

TEST(matcher, partial_consumption_on_garbage_tail) {
  auto cands = MakeMatcher().Match("nihaox");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "你好");
  ASSERT_EQ(cands[0].consumed, 5);
}

TEST(matcher, empty_or_unmatchable_input) {
  ASSERT(MakeMatcher().Match("").empty());
  ASSERT(MakeMatcher().Match("xq").empty());
}

TEST(matcher, respects_max_candidates) {
  auto cands = MakeMatcher({}, 2).Match("nihaoshijie");
  ASSERT(cands.size() <= 2u);
}
