#include <naive_pinyin/c_api.h>
#include <naive_pinyin/naive_pinyin.h>

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "test_framework.h"

namespace {

const char* kTestDict = R"(
ni	你:920,尼:800,呢:780
hao	好:880,号:850
ni hao	你好:758
shi	世:900,是:950,十:880
jie	界:870,节:860
shi jie	世界:782,使节:566
)";

naive_pinyin::Engine* NewEngineWithDict(const std::string& config) {
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson(config, &err);
  if (!engine) throw std::runtime_error("create failed: " + err);
  if (!engine->LoadDict(kTestDict, std::strlen(kTestDict))) {
    throw std::runtime_error("dict load failed");
  }
  return engine.release();
}

}  // namespace

TEST(engine, create_and_query_empty_without_dict) {
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson("{}", &err);
  ASSERT(engine != nullptr);
  auto j = nlohmann::json::parse(engine->Query("nihao"));
  ASSERT(j.contains("candidates"));
  ASSERT(j["candidates"].empty());
}

TEST(engine, rejects_bad_config) {
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson("{bad", &err);
  ASSERT(engine == nullptr);
  ASSERT(!err.empty());
}

TEST(engine, rejects_invalid_input_chars) {
  auto engine = naive_pinyin::Engine::CreateFromJson("{}");
  std::string result = engine->Query("Ni3");
  ASSERT(result.find("error") != std::string::npos);
}

TEST(engine, query_returns_json_candidates) {
  std::unique_ptr<naive_pinyin::Engine> engine(NewEngineWithDict("{}"));
  auto j = nlohmann::json::parse(engine->Query("nihaoshijie"));
  ASSERT(!j["candidates"].empty());
  ASSERT_EQ(j["candidates"][0]["text"], "你好世界");
  ASSERT_EQ(j["candidates"][0]["consumed"], 11);
}

TEST(engine, user_words_boosted) {
  const char* dict = "na\t那:900\nyi\t一:950\nge\t个:950\n";
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson(R"({
    "user_words": [{"pinyin": "na yi ge", "word": "那一个", "freq": 1000}]
  })", &err);
  ASSERT(engine != nullptr);
  ASSERT(engine->LoadDict(dict, std::strlen(dict)));
  auto j = nlohmann::json::parse(engine->Query("nayige"));
  ASSERT(!j["candidates"].empty());
  ASSERT_EQ(j["candidates"][0]["text"], "那一个");
}

TEST(c_api, roundtrip) {
  void* ctx = np_create("{}");
  ASSERT(ctx != nullptr);

  ASSERT_EQ(np_load_dict(ctx, kTestDict,
                         static_cast<int>(std::strlen(kTestDict))), 1);

  const char* result = np_query(ctx, "nihao");
  ASSERT(result != nullptr);
  auto j = nlohmann::json::parse(result);
  ASSERT_EQ(j["candidates"][0]["text"], "你好");

  np_destroy(ctx);
}

TEST(c_api, null_safety) {
  ASSERT(np_create(nullptr) == nullptr);
  ASSERT_EQ(np_load_dict(nullptr, "x", 1), 0);
  ASSERT(np_query(nullptr, "x") == nullptr);
  np_destroy(nullptr);  // 不应崩溃
}

TEST(engine, segment_full_pinyin) {
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson("{}", &err);
  ASSERT(engine != nullptr);

  // DAG 全边界：wo|de|dao|dun 主路径外，da(6)、du(9) 等替代音节边也可达。
  auto j = nlohmann::json::parse(engine->Segment("wodedaodun"));
  ASSERT(j.contains("boundaries"));
  std::vector<int> b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 6, 7, 9, 10}));

  // 歧义切分 fan|gan 与 fang|an 两个边界都在；fa/ga 也是音节，2/5 可达。
  j = nlohmann::json::parse(engine->Segment("fangan"));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 3, 4, 5, 6}));

  // 末尾半截音节不构成边，但末尾本身永远是一站。
  j = nlohmann::json::parse(engine->Segment("woded"));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 5}));

  // apostrophe 硬边界：xi'an， apostrophe 前是一站，不可跨越。
  j = nlohmann::json::parse(engine->Segment("xi'an"));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 5}));

  // 主切分路径：贪心最长边。
  j = nlohmann::json::parse(engine->Segment("wodedaodun"));
  b = j["path"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 7, 10}));
  j = nlohmann::json::parse(engine->Segment("xi'an"));
  b = j["path"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 3, 5}));  // apostrophe 自成一站

  // 空输入与非法输入。
  j = nlohmann::json::parse(engine->Segment(""));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0}));
  j = nlohmann::json::parse(engine->Segment("Ni1"));
  ASSERT(j.contains("error"));
}

TEST(engine, segment_shuangpin) {
  std::string err;
  // 最小双拼码表：wo/de/dk/dp + aa。
  auto engine = naive_pinyin::Engine::CreateFromJson(R"(
    {"shuangpin": {"map": {"wo": "wo", "de": "de", "dk": "dao",
                           "dp": "dun", "aa": "a"}}}
  )", &err);
  ASSERT(engine != nullptr);

  // 恒 2 键一站。
  auto j = nlohmann::json::parse(engine->Segment("wodedkdp"));
  std::vector<int> b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 6, 8}));

  // 奇数长度：末尾仍是站。
  j = nlohmann::json::parse(engine->Segment("woded"));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 5}));

  // 未命中码表的位置无边（死路），末尾兜底。
  j = nlohmann::json::parse(engine->Segment("wozzdp"));
  b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 6}));
}

TEST(c_api, segment_roundtrip) {
  void* ctx = np_create("{}");
  ASSERT(ctx != nullptr);
  const char* result = np_segment(ctx, "nihao");
  ASSERT(result != nullptr);
  auto j = nlohmann::json::parse(result);
  std::vector<int> b = j["boundaries"].get<std::vector<int>>();
  ASSERT_EQ(b, (std::vector<int>{0, 2, 4, 5}));  // ni|hao 与 ni|ha|o 两条路径
  ASSERT(np_segment(nullptr, "x") == nullptr);
  np_destroy(ctx);
}
