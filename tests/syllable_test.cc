#include "../naive_pinyin/src/syllable.h"

#include "test_framework.h"

using naive_pinyin::IsSyllable;
using naive_pinyin::SegmentFullPinyin;
using naive_pinyin::Segmentation;
using naive_pinyin::SplitSyllable;

TEST(syllable, valid_and_invalid) {
  ASSERT(IsSyllable("ni"));
  ASSERT(IsSyllable("hao"));
  ASSERT(IsSyllable("zhuang"));
  ASSERT(IsSyllable("a"));
  ASSERT(IsSyllable("lve"));
  ASSERT(IsSyllable("biang"));
  ASSERT(!IsSyllable("nih"));
  ASSERT(!IsSyllable("xyz"));
  ASSERT(!IsSyllable(""));
}

TEST(syllable, split_initial_final) {
  std::string ini, fin;
  SplitSyllable("zhuang", &ini, &fin);
  ASSERT_EQ(ini, "zh");
  ASSERT_EQ(fin, "uang");
  SplitSyllable("ni", &ini, &fin);
  ASSERT_EQ(ini, "n");
  ASSERT_EQ(fin, "i");
  SplitSyllable("an", &ini, &fin);
  ASSERT_EQ(ini, "");
  ASSERT_EQ(fin, "an");
  SplitSyllable("shi", &ini, &fin);
  ASSERT_EQ(ini, "sh");
  ASSERT_EQ(fin, "i");
}

namespace {

std::vector<std::string> SyllablesAt(const Segmentation& seg, int pos) {
  std::vector<std::string> out;
  for (const auto& e : seg.edges_from[pos]) out.push_back(e.syllable);
  return out;
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
  for (const auto& x : v) if (x == s) return true;
  return false;
}

}  // namespace

TEST(syllable, segment_basic_dag) {
  Segmentation seg = SegmentFullPinyin("nihao");
  ASSERT_EQ(seg.length, 5);
  // 位置 0: 只有 "ni"
  ASSERT_EQ(SyllablesAt(seg, 0).size(), 1u);
  ASSERT(Has(SyllablesAt(seg, 0), "ni"));
  // 位置 2: "hao" 和 "ha"
  ASSERT_EQ(SyllablesAt(seg, 2).size(), 2u);
  ASSERT(Has(SyllablesAt(seg, 2), "hao"));
  ASSERT(Has(SyllablesAt(seg, 2), "ha"));
  // 位置 3: "ao" 和 "a"
  ASSERT_EQ(SyllablesAt(seg, 3).size(), 2u);
  ASSERT(Has(SyllablesAt(seg, 3), "ao"));
  ASSERT(Has(SyllablesAt(seg, 3), "a"));
  // 位置 4: "o"
  ASSERT_EQ(SyllablesAt(seg, 4).size(), 1u);
  ASSERT(Has(SyllablesAt(seg, 4), "o"));
}

TEST(syllable, segment_ambiguous_xian) {
  Segmentation seg = SegmentFullPinyin("xian");
  // 位置 0: "xi" / "xia" / "xian"
  ASSERT_EQ(SyllablesAt(seg, 0).size(), 3u);
  ASSERT(Has(SyllablesAt(seg, 0), "xi"));
  ASSERT(Has(SyllablesAt(seg, 0), "xia"));
  ASSERT(Has(SyllablesAt(seg, 0), "xian"));
  // 位置 2: "an" 和 "a"
  ASSERT_EQ(SyllablesAt(seg, 2).size(), 2u);
}

TEST(syllable, apostrophe_is_hard_boundary) {
  Segmentation seg = SegmentFullPinyin("xi'an");
  ASSERT_EQ(seg.length, 5);
  ASSERT(seg.boundary[2]);
  // 位置 0 只有 "xi"（"xia"/"xian" 被 apostrophe 阻断）
  ASSERT_EQ(SyllablesAt(seg, 0).size(), 1u);
  ASSERT(Has(SyllablesAt(seg, 0), "xi"));
  // 位置 3: "an" 和 "a"
  ASSERT_EQ(SyllablesAt(seg, 3).size(), 2u);
  ASSERT(Has(SyllablesAt(seg, 3), "an"));
}

TEST(syllable, no_valid_edges_for_garbage) {
  Segmentation seg = SegmentFullPinyin("xq");
  ASSERT(seg.edges_from[0].empty());
  ASSERT(seg.edges_from[1].empty());
}
