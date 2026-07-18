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

EngineImpl::EngineImpl(Config config)
    : config_(std::move(config)), user_freq_(config_.user_boost) {}

bool EngineImpl::LoadDict(const char* data, size_t size) {
  if (!dict_.Load(data, size)) return false;
  // 用户自定义词并入词典（freq 直接作为 0..1000 的分数）。
  for (const UserWord& w : config_.user_words) {
    int score = static_cast<int>(w.freq);
    if (score < 0) score = 0;
    if (score > 1000) score = 1000;
    dict_.AddEntry(w.pinyin, w.word, score);
  }
  // 回传的调频叠加层：词典没有的 (key,word) 视为自造词注册进 trie。
  for (const UserFreqEntry& e : config_.user_freq) {
    if (!dict_.HasEntry(e.pinyin, e.word)) {
      dict_.AddEntry(e.pinyin, e.word, 500);
    }
  }
  user_freq_.Load(config_.user_freq);
  return true;
}

void EngineImpl::Commit(const std::string& segments_json) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(segments_json);
  } catch (const nlohmann::json::exception&) {
    return;  // 静默忽略
  }
  if (!j.is_array()) return;
  for (const auto& seg : j) {
    if (!seg.contains("key") || !seg.contains("word")) continue;
    user_freq_.Commit(seg.at("key").get<std::string>(),
                      seg.at("word").get<std::string>());
  }
}

void EngineImpl::LearnWord(const std::string& key, const std::string& word) {
  if (!dict_.HasEntry(key, word)) {
    dict_.AddEntry(key, word, 500);
  }
  user_freq_.Commit(key, word);
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
                  config_.segment_penalty,
                  config_.shuangpin_map.empty() ? nullptr
                                                : &config_.shuangpin_map,
                  &user_freq_);
  std::vector<MatchCandidate> candidates = matcher.Match(input);

  nlohmann::json out;
  out["input"] = input;
  out["candidates"] = nlohmann::json::array();
  for (const MatchCandidate& c : candidates) {
    nlohmann::json segs = nlohmann::json::array();
    for (const auto& [key, word] : c.segments) {
      segs.push_back({{"key", key}, {"word", word}});
    }
    out["candidates"].push_back({
        {"text", c.text},
        {"consumed", c.consumed},
        {"score", c.score},
        {"segments", segs},
    });
  }
  return out.dump();
}

}  // namespace naive_pinyin
