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
