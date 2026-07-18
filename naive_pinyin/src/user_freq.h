// 用户词频叠加层：(拼音key, 词) -> 选择次数
// 有效分 = 静态分 + Boost(count)；可导出/重载（localStorage 持久化）。
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace naive_pinyin {

struct UserFreqEntry {
  std::string pinyin;  // 空格分隔音节 key
  std::string word;
  int count = 0;
};

struct UserBoostParams {
  int first = 300;  // 首次提交加分
  int inc = 50;     // 后续每次加分
  int max = 600;    // 单词加分上限
};

class UserFreq {
 public:
  explicit UserFreq(UserBoostParams params = {}) : params_(params) {}

  // 提交一次选择（count++）。
  void Commit(const std::string& key, const std::string& word);

  // 该 (key, word) 当前的加分值。
  int Boost(const std::string& key, const std::string& word) const;

  int Count(const std::string& key, const std::string& word) const;

  // 批量载入（config user_freq 回传）。
  void Load(const std::vector<UserFreqEntry>& entries);

  // 导出为 JSON: [{"pinyin":"...","word":"...","count":n}, ...]
  std::string Dump() const;

  size_t size() const { return counts_.size(); }

 private:
  UserBoostParams params_;
  // 编码为 key + "\t" + word（两者都不含制表符）。
  std::unordered_map<std::string, int> counts_;
};

}  // namespace naive_pinyin
