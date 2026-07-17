#include "../naive_pinyin/src/config.h"

#include "test_framework.h"

using naive_pinyin::Config;

TEST(config, empty_json_gives_defaults) {
  Config cfg = Config::FromJson("{}");
  ASSERT(cfg.shuangpin_map.empty());
  ASSERT(cfg.fuzzy.empty());
  ASSERT_EQ(cfg.max_candidates, 10);
  ASSERT(cfg.user_words.empty());
}

TEST(config, parses_all_fields) {
  Config cfg = Config::FromJson(R"({
    "shuangpin": {"map": {"ni": "ni", "hk": "hao"}},
    "fuzzy": [["z", "zh"], ["n", "l"]],
    "max_candidates": 5,
    "user_words": [{"pinyin": "na yi ge", "word": "那一个", "freq": 42}]
  })");
  ASSERT_EQ(cfg.shuangpin_map.size(), 2u);
  ASSERT_EQ(cfg.shuangpin_map.at("hk"), "hao");
  ASSERT_EQ(cfg.fuzzy.size(), 2u);
  ASSERT_EQ(cfg.fuzzy[0].first, "z");
  ASSERT_EQ(cfg.fuzzy[0].second, "zh");
  ASSERT_EQ(cfg.max_candidates, 5);
  ASSERT_EQ(cfg.user_words.size(), 1u);
  ASSERT_EQ(cfg.user_words[0].word, "那一个");
  ASSERT_EQ(cfg.user_words[0].freq, 42.0);
}

TEST(config, rejects_invalid_json) {
  bool threw = false;
  try {
    Config::FromJson("{not json");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  ASSERT(threw);
}
