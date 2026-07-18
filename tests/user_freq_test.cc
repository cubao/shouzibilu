#include "../naive_pinyin/src/user_freq.h"

#include <cstring>
#include <string>

#include <naive_pinyin/c_api.h>
#include <nlohmann/json.hpp>

#include "test_framework.h"

using naive_pinyin::UserFreq;
using naive_pinyin::UserFreqEntry;

namespace {

const char* kTestDict = R"(
ji	级:859,集:851,几:817,极:751
nan	难:791,南:752
ji nan	极难:558,济南:526
zhong	中:950,重:900
chong	冲:900,重:800
tang	汤:820,唐:800
zhi	知:880,志:800
xiong	熊:820,雄:800
)";

void* NewCtx() {
  void* ctx = np_create("{}");
  if (!ctx) throw std::runtime_error("np_create failed");
  if (np_load_dict(ctx, kTestDict, static_cast<int>(std::strlen(kTestDict))) != 1) {
    throw std::runtime_error("np_load_dict failed");
  }
  return ctx;
}

nlohmann::json Query(void* ctx, const char* input) {
  return nlohmann::json::parse(np_query(ctx, input));
}

}  // namespace

TEST(user_freq, boost_formula_and_cap) {
  UserFreq uf({300, 50, 600});
  ASSERT_EQ(uf.Boost("ji nan", "济南"), 0);
  uf.Commit("ji nan", "济南");
  ASSERT_EQ(uf.Boost("ji nan", "济南"), 300);   // 首次 +300
  uf.Commit("ji nan", "济南");
  ASSERT_EQ(uf.Boost("ji nan", "济南"), 350);   // 后续 +50
  for (int i = 0; i < 20; ++i) uf.Commit("ji nan", "济南");
  ASSERT_EQ(uf.Boost("ji nan", "济南"), 600);   // 上限 600
  // 其他 (key,word) 不受影响
  ASSERT_EQ(uf.Boost("ji nan", "极难"), 0);
  ASSERT_EQ(uf.Boost("zhong", "济南"), 0);
}

TEST(user_freq, dump_and_load_roundtrip) {
  UserFreq uf;
  uf.Commit("ji nan", "济南");
  uf.Commit("ji nan", "济南");
  uf.Commit("zhong", "重");
  std::string dump = uf.Dump();

  auto j = nlohmann::json::parse(dump);
  ASSERT_EQ(j.size(), 2u);

  UserFreq uf2;
  std::vector<UserFreqEntry> entries;
  for (const auto& e : j) {
    entries.push_back({e.at("pinyin").get<std::string>(),
                       e.at("word").get<std::string>(),
                       e.at("count").get<int>()});
  }
  uf2.Load(entries);
  ASSERT_EQ(uf2.Count("ji nan", "济南"), 2);
  ASSERT_EQ(uf2.Count("zhong", "重"), 1);
}

TEST(user_freq, commit_flips_ranking) {
  void* ctx = NewCtx();
  // 静态: 极难(558) > 济南(526)
  ASSERT_EQ(Query(ctx, "jinan")["candidates"][0]["text"], "极难");
  // 提交一次济南 -> +300 反超
  np_commit(ctx, R"([{"key":"ji nan","word":"济南"}])");
  ASSERT_EQ(Query(ctx, "jinan")["candidates"][0]["text"], "济南");
  np_destroy(ctx);
}

TEST(user_freq, polyphonic_keys_independent) {
  void* ctx = NewCtx();
  // 提交 zhong 下的 重
  np_commit(ctx, R"([{"key":"zhong","word":"重"}])");
  // zhong: 重(900+300=1200) 超过 中(950)
  ASSERT_EQ(Query(ctx, "zhong")["candidates"][0]["text"], "重");
  // chong: 重 不加分, 冲(900) 仍第一
  ASSERT_EQ(Query(ctx, "chong")["candidates"][0]["text"], "冲");
  np_destroy(ctx);
}

TEST(user_freq, learn_word_enters_candidates) {
  void* ctx = NewCtx();
  auto before = Query(ctx, "tangzhixiong")["candidates"];
  for (const auto& c : before) ASSERT(c["text"] != "唐志雄");

  np_learn_word(ctx, "tang zhi xiong", "唐志雄");
  auto after = Query(ctx, "tangzhixiong")["candidates"];
  ASSERT(!after.empty());
  ASSERT_EQ(after[0]["text"], "唐志雄");
  np_destroy(ctx);
}

TEST(user_freq, reload_via_config) {
  void* ctx = NewCtx();
  np_commit(ctx, R"([{"key":"ji nan","word":"济南"}])");
  np_learn_word(ctx, "tang zhi xiong", "唐志雄");
  std::string dump = np_dump_user(ctx);
  np_destroy(ctx);

  // 新引擎: user_freq 经 config 回传
  std::string config = std::string("{\"user_freq\":") + dump + "}";
  void* ctx2 = np_create(config.c_str());
  ASSERT(ctx2 != nullptr);
  ASSERT_EQ(np_load_dict(ctx2, kTestDict,
                         static_cast<int>(std::strlen(kTestDict))), 1);
  // 调频效果保留
  ASSERT_EQ(Query(ctx2, "jinan")["candidates"][0]["text"], "济南");
  // 自造词保留
  ASSERT_EQ(Query(ctx2, "tangzhixiong")["candidates"][0]["text"], "唐志雄");
  np_destroy(ctx2);
}

TEST(user_freq, candidate_json_has_segments) {
  void* ctx = NewCtx();
  auto j = Query(ctx, "jinan");
  const auto& cand = j["candidates"][0];
  ASSERT(cand.contains("segments"));
  ASSERT_EQ(cand["segments"].size(), 1u);
  ASSERT_EQ(cand["segments"][0]["key"], "ji nan");
  ASSERT(cand["segments"][0]["word"] == "极难" ||
         cand["segments"][0]["word"] == "济南");
  np_destroy(ctx);
}

TEST(user_freq, commit_ignores_garbage) {
  void* ctx = NewCtx();
  np_commit(ctx, "not json");
  np_commit(ctx, "{}");
  np_commit(ctx, nullptr);
  np_learn_word(ctx, nullptr, "x");
  // 不崩溃, 查询正常
  ASSERT_EQ(Query(ctx, "jinan")["candidates"][0]["text"], "极难");
  np_destroy(ctx);
}
