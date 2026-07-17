// Engine 实现（内部头）
#pragma once

#include "config.h"
#include "dict.h"
#include <naive_pinyin/naive_pinyin.h>

namespace naive_pinyin {

class EngineImpl : public Engine {
 public:
  explicit EngineImpl(Config config);

  bool LoadDict(const char* data, size_t size) override;
  std::string Query(const std::string& input) const override;

 private:
  Config config_;
  Dict dict_;
};

}  // namespace naive_pinyin
