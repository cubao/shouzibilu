#include "../naive_pinyin/src/dict.h"

#include <cstring>

#include "test_framework.h"

using naive_pinyin::Dict;

namespace {

const Dict::Node* Walk(const Dict::Node* node, const char* syl) {
  auto it = node->children.find(syl);
  return it == node->children.end() ? nullptr : it->second.get();
}

}  // namespace

TEST(dict, load_and_lookup) {
  const char* text =
      "# comment\n"
      "\n"
      "ni\t你:920,尼:800\n"
      "ni hao\t你好:758,尼号:490\n"
      "zhong hua ren min gong he guo\t中华人民共和国:782\n";
  Dict dict;
  ASSERT(dict.Load(text, std::strlen(text)));
  ASSERT_EQ(dict.num_entries(), 5u);
  ASSERT_EQ(dict.max_key_length(), 7);

  const Dict::Node* ni = Walk(dict.root(), "ni");
  ASSERT(ni != nullptr);
  ASSERT_EQ(ni->entries.size(), 2u);
  ASSERT_EQ(ni->entries[0].word, "你");
  ASSERT_EQ(ni->entries[0].score, 920);

  const Dict::Node* hao = Walk(ni, "hao");
  ASSERT(hao != nullptr);
  ASSERT_EQ(hao->entries.size(), 2u);
  ASSERT_EQ(hao->entries[0].word, "你好");

  ASSERT(Walk(dict.root(), "xyz") == nullptr);
}

TEST(dict, skips_malformed_lines) {
  const char* text =
      "no-tab-here\n"
      "\t\n"
      "ni\t你:920\n";
  Dict dict;
  ASSERT(dict.Load(text, std::strlen(text)));
  ASSERT_EQ(dict.num_entries(), 1u);
}

TEST(dict, add_entry_updates_and_keeps_order) {
  Dict dict;
  dict.AddEntry("ni hao", "你好", 100);
  dict.AddEntry("ni hao", "尼号", 500);
  ASSERT_EQ(dict.num_entries(), 2u);
  // 更新已有词：分数提升后重排
  dict.AddEntry("ni hao", "你好", 900);
  ASSERT_EQ(dict.num_entries(), 2u);
  const Dict::Node* node = Walk(Walk(dict.root(), "ni"), "hao");
  ASSERT_EQ(node->entries.size(), 2u);
  ASSERT_EQ(node->entries[0].word, "你好");
  ASSERT_EQ(node->entries[0].score, 900);
}

TEST(dict, empty_load_fails) {
  Dict dict;
  ASSERT(!dict.Load("", 0));
  ASSERT(!dict.Load(nullptr, 0));
}
