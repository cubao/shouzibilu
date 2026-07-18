// Engine 实现（内部头）
#pragma once

#include "config.h"
#include "dict.h"
#include "user_freq.h"
#include <naive_pinyin/naive_pinyin.h>

namespace naive_pinyin {

class EngineImpl : public Engine {
 public:
  explicit EngineImpl(Config config);

  bool LoadDict(const char* data, size_t size) override;
  std::string Query(const std::string& input) const override;

  // 提交一个候选的分段（动态调频）。segments_json 形如
  // [{"key":"ni hao","word":"你好"}, ...]，解析失败静默忽略。
  void Commit(const std::string& segments_json);

  // 学习自造词：词典没有则注册（基础分 500），并记一次提交。
  void LearnWord(const std::string& key, const std::string& word);

  // 导出叠加层 JSON（持久化用）。
  std::string DumpUser() const { return user_freq_.Dump(); }

 private:
  Config config_;
  Dict dict_;
  UserFreq user_freq_;
};

}  // namespace naive_pinyin
