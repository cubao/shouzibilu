#include <cstring>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../naive_pinyin/src/matcher.h"
#include "../naive_pinyin/src/syllable.h"
#include "test_framework.h"

using naive_pinyin::Dict;
using naive_pinyin::Matcher;
using naive_pinyin::SegmentShuangpin;
using naive_pinyin::Segmentation;

namespace {

// 测试用小码表（自然码子集）:
//   ni->ni  hk->hao  ui->shi  jx->jie  xi->xi  an->an  xm->xian
const std::unordered_map<std::string, std::string> kMap = {
    {"ni", "ni"}, {"hk", "hao"}, {"ui", "shi"}, {"jx", "jie"},
    {"xi", "xi"}, {"an", "an"}, {"xm", "xian"},
};

const char* kTestDict = R"(
ni	你:920,尼:800
hao	好:880,号:850
ni hao	你好:758
shi	世:900,是:950
jie	界:870,节:860
shi jie	世界:782
xian	先:900
xi	西:880
an	安:870
xi an	西安:700
)";

Dict& GetDict() {
  static Dict dict = [] {
    Dict d;
    d.Load(kTestDict, std::strlen(kTestDict));
    return d;
  }();
  return dict;
}

std::vector<std::string> SyllablesAt(const Segmentation& seg, int pos) {
  std::vector<std::string> out;
  for (const auto& e : seg.edges_from[pos]) out.push_back(e.syllable);
  return out;
}

}  // namespace

TEST(shuangpin, segment_two_letter_codes) {
  Segmentation seg = SegmentShuangpin("nihk", kMap);
  ASSERT_EQ(seg.length, 4);
  ASSERT_EQ(SyllablesAt(seg, 0).size(), 1u);
  ASSERT_EQ(SyllablesAt(seg, 0)[0], "ni");
  ASSERT_EQ(SyllablesAt(seg, 2).size(), 1u);
  ASSERT_EQ(SyllablesAt(seg, 2)[0], "hao");
}

TEST(shuangpin, segment_apostrophe_and_dead_ends) {
  // apostrophe 分隔
  Segmentation seg = SegmentShuangpin("ni'hk", kMap);
  ASSERT(seg.boundary[2]);
  ASSERT_EQ(SyllablesAt(seg, 0)[0], "ni");
  ASSERT_EQ(SyllablesAt(seg, 3)[0], "hao");

  // 奇数长度段: 末尾字母死路
  Segmentation odd = SegmentShuangpin("nih", kMap);
  ASSERT_EQ(SyllablesAt(odd, 0).size(), 1u);
  ASSERT(SyllablesAt(odd, 2).empty());

  // 未命中码表: 无边
  Segmentation bad = SegmentShuangpin("nizz", kMap);
  ASSERT_EQ(SyllablesAt(bad, 0).size(), 1u);
  ASSERT(SyllablesAt(bad, 2).empty());
}

TEST(shuangpin, match_word_and_sentence) {
  Matcher matcher(GetDict(), {}, 10, 1100, &kMap);

  auto word = matcher.Match("nihk");
  ASSERT(!word.empty());
  ASSERT_EQ(word[0].text, "你好");
  ASSERT_EQ(word[0].consumed, 4);

  auto sent = matcher.Match("nihkuijx");
  ASSERT(!sent.empty());
  ASSERT_EQ(sent[0].text, "你好世界");
  ASSERT_EQ(sent[0].consumed, 8);
}

TEST(shuangpin, match_with_apostrophe) {
  Matcher matcher(GetDict(), {}, 10, 1100, &kMap);
  auto cands = matcher.Match("ni'hk");
  ASSERT(!cands.empty());
  ASSERT_EQ(cands[0].text, "你好");
  ASSERT_EQ(cands[0].consumed, 5);
}

TEST(shuangpin, fuzzy_still_applies) {
  // 双拼转出的音节照样走模糊音: ui->shi, 模糊 s/sh 后也能匹配 si 系
  Matcher matcher(GetDict(), {{"s", "sh"}}, 10, 1100, &kMap);
  auto cands = matcher.Match("ui");
  ASSERT(!cands.empty());
  // "ui" 码 -> shi 音节; 候选应含 世/是
  bool found = false;
  for (const auto& c : cands) found |= (c.text == "世") || (c.text == "是");
  ASSERT(found);
}
