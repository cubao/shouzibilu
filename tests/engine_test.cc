#include <naive_pinyin/c_api.h>
#include <naive_pinyin/naive_pinyin.h>

#include <cstring>
#include <string>

#include "test_framework.h"

TEST(engine, create_and_query_empty) {
  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson("{}", &err);
  ASSERT(engine != nullptr);
  ASSERT_EQ(engine->Query("nihao"), "[]");
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

TEST(c_api, roundtrip) {
  void* ctx = np_create("{}");
  ASSERT(ctx != nullptr);

  const char* dict = "ni hao\t你好:100\n";
  ASSERT_EQ(np_load_dict(ctx, dict, static_cast<int>(std::strlen(dict))), 1);

  const char* result = np_query(ctx, "nihao");
  ASSERT(result != nullptr);
  ASSERT_EQ(std::string(result), "[]");

  np_destroy(ctx);
}

TEST(c_api, null_safety) {
  ASSERT(np_create(nullptr) == nullptr);
  ASSERT_EQ(np_load_dict(nullptr, "x", 1), 0);
  ASSERT(np_query(nullptr, "x") == nullptr);
  np_destroy(nullptr);  // 不应崩溃
}
