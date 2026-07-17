#include "engine.h"

#include <nlohmann/json.hpp>

namespace naive_pinyin {

std::unique_ptr<Engine> Engine::CreateFromJson(const std::string& config_json,
                                               std::string* error) {
  try {
    Config cfg = Config::FromJson(config_json);
    return std::unique_ptr<Engine>(new EngineImpl(std::move(cfg)));
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return nullptr;
  }
}

EngineImpl::EngineImpl(Config config) : config_(std::move(config)) {}

bool EngineImpl::LoadDict(const char* data, size_t size) {
  // M0 骨架：仅记录收到数据。M2 实现真正的词典解析。
  dict_loaded_ = (data != nullptr && size > 0);
  return dict_loaded_;
}

std::string EngineImpl::Query(const std::string& input) const {
  // 输入校验：只接受小写字母和音节分隔符。
  for (char ch : input) {
    if (!(ch >= 'a' && ch <= 'z') && ch != '\'') {
      nlohmann::json err;
      err["error"] = "invalid input: only a-z and ' allowed";
      err["input"] = input;
      return err.dump();
    }
  }
  // M0 骨架：M2 实现音节切分 + DP 匹配。
  return "[]";
}

}  // namespace naive_pinyin
