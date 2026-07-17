#include "engine.h"

#include <nlohmann/json.hpp>

#include "matcher.h"

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
  if (!dict_.Load(data, size)) return false;
  // 用户自定义词并入词典（freq 直接作为 0..1000 的分数）。
  for (const UserWord& w : config_.user_words) {
    int score = static_cast<int>(w.freq);
    if (score < 0) score = 0;
    if (score > 1000) score = 1000;
    dict_.AddEntry(w.pinyin, w.word, score);
  }
  return true;
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

  Matcher matcher(dict_, config_.fuzzy, config_.max_candidates,
                  config_.segment_penalty);
  std::vector<MatchCandidate> candidates = matcher.Match(input);

  nlohmann::json out;
  out["input"] = input;
  out["candidates"] = nlohmann::json::array();
  for (const MatchCandidate& c : candidates) {
    out["candidates"].push_back({
        {"text", c.text},
        {"consumed", c.consumed},
        {"score", c.score},
    });
  }
  return out.dump();
}

}  // namespace naive_pinyin
