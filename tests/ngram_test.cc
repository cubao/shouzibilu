#include "../naive_pinyin/src/ngram.h"

#include <cstring>
#include <string>

#include "msng_builder.h"
#include "test_framework.h"

using namespace msng_test_util;
using naive_pinyin::NgramTable;

TEST(ngram, lookup_round_trips) {
  const std::string data = BuildTable(
      {{HashPair("配置", "与"), 2.5f}, {HashPair("\x01", "本仓"), -1.25f}});
  NgramTable table;
  ASSERT(table.Load(data.data(), data.size()));
  ASSERT_EQ(table.size(), 2u);
  const std::string a = "配置", b = "与";
  const std::string ab[2] = {a, b};
  ASSERT((table.Increment(ab, 2) - 2.5f) < 0.01f);
  const std::string ss = "\x01", c = "本仓";
  const std::string sc[2] = {ss, c};
  ASSERT(table.Increment(sc, 2) < -1.24f);
}

TEST(ngram, miss_scores_zero_and_pair_is_ordered) {
  const std::string data = BuildTable({{HashPair("配置", "与"), 2.5f}});
  NgramTable table;
  ASSERT(table.Load(data.data(), data.size()));
  const std::string p = "配置", q = "与", r = "于", empty = "";
  const std::string pq[2] = {p, q};
  const std::string qp[2] = {q, p};
  const std::string pr[2] = {p, r};
  const std::string pe[2] = {p, empty};
  ASSERT((table.Increment(pq, 2) - 2.5f) < 0.01f);
  ASSERT(table.Increment(qp, 2) == 0.0f);   // 有序对，不是集合
  ASSERT(table.Increment(pr, 2) == 0.0f);   // 缺失 = 0（不罚分）
  ASSERT(table.Increment(pe, 2) == 0.0f);   // 末词为空 = 0
}

TEST(ngram, trigram_uses_three_words) {
  const std::string data =
      BuildTable({{HashTriple("\x01", "输入", "法"), 4.0f},
                  {HashTriple("的", "输入", "法"), 1.5f}});
  NgramTable table;
  ASSERT(table.Load(data.data(), data.size()));
  const std::string ss = "\x01", de = "的", a = "输入", b = "法", c = "码";
  const std::string t1[3] = {ss, a, b};
  const std::string t2[3] = {de, a, b};
  const std::string t3[3] = {a, ss, b};  // 词序错了查不到
  const std::string t4[3] = {ss, a, c};
  ASSERT((table.Increment(t1, 3) - 4.0f) < 0.01f);
  ASSERT((table.Increment(t2, 3) - 1.5f) < 0.01f);
  ASSERT(table.Increment(t3, 3) == 0.0f);
  ASSERT(table.Increment(t4, 3) == 0.0f);
}

TEST(ngram, duplicate_keys_take_first) {
  // 升序（允许相等）的重复键：查找取第一条。
  const uint64_t key = HashPair("配", "置");
  const std::string data = BuildTable({{key, 1.0f}, {key, 2.0f}});
  NgramTable table;
  ASSERT(table.Load(data.data(), data.size()));
  const std::string p = "配", q = "置";
  const std::string pq[2] = {p, q};
  ASSERT((table.Increment(pq, 2) - 1.0f) < 0.01f);
}

TEST(ngram, rejects_bad_files) {
  const auto load_fails = [](const std::string& bytes) {
    NgramTable t;
    return !t.Load(bytes.data(), bytes.size());
  };
  ASSERT(load_fails(""));  // 空文件
  ASSERT(load_fails(std::string("MSNG\x01\0\0", 7)));  // 短于头

  const std::string good = BuildTable({{1, 1.0f}, {2, 2.0f}});
  // 错 magic
  std::string bad_magic = good;
  bad_magic[0] = 'X';
  ASSERT(load_fails(bad_magic));
  // 未来版本
  std::string bad_version = good;
  bad_version[5] = 99;
  ASSERT(load_fails(bad_version));
  // 无序键
  NgramTable t;
  std::string unsorted;
  unsorted += "MSNG";
  AppendU32(&unsorted, 1);
  AppendU32(&unsorted, 2);
  AppendU32(&unsorted, 0);
  AppendU64(&unsorted, 9);
  AppendU64(&unsorted, 2);
  AppendF32(&unsorted, 1.0f);
  AppendF32(&unsorted, 2.0f);
  ASSERT(load_fails(unsorted));
  // 截断
  ASSERT(load_fails(good.substr(0, good.size() - 6)));
  // 尾部多余字节合法（只检查 needed <= size）
  std::string trailing = good;
  trailing += "xxxxx";
  ASSERT(t.Load(trailing.data(), trailing.size()));
  // count 超上限
  std::string oversized;
  oversized += "MSNG";
  AppendU32(&oversized, 1);
  AppendU32(&oversized, 40 * 1000 * 1000 + 1);
  AppendU32(&oversized, 0);
  ASSERT(load_fails(oversized));
  // 空表合法
  const std::string empty = BuildTable({});
  NgramTable empty_table;
  ASSERT(empty_table.Load(empty.data(), empty.size()));
  ASSERT(empty_table.empty());
}

TEST(ngram, loads_real_msime_table_shape) {
  // 与官方表同构的小表：16B 头 + 排序键 + f32 值；句首键 \x01 前词可查。
  const std::string data = BuildTable(
      {{HashPair("\x01", "中文"), 0.5f}, {HashPair("中文", "输入"), 0.25f}});
  NgramTable table;
  ASSERT(table.Load(data.data(), data.size()));
  const std::string ss = "\x01", a = "中文", b = "输入";
  const std::string t1[2] = {ss, a};
  const std::string t2[2] = {a, b};
  ASSERT((table.Increment(t1, 2) - 0.5f) < 0.01f);
  ASSERT((table.Increment(t2, 2) - 0.25f) < 0.01f);
}
