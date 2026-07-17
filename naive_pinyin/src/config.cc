#include "config.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace naive_pinyin {

Config Config::FromJson(const std::string& json_str) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("invalid config json: ") + e.what());
  }

  Config cfg;

  if (j.contains("shuangpin")) {
    const auto& sp = j.at("shuangpin");
    if (sp.contains("map")) {
      for (auto it = sp.at("map").begin(); it != sp.at("map").end(); ++it) {
        cfg.shuangpin_map[it.key()] = it.value().get<std::string>();
      }
    }
  }

  if (j.contains("fuzzy")) {
    for (const auto& pair : j.at("fuzzy")) {
      if (!pair.is_array() || pair.size() != 2) {
        throw std::runtime_error("fuzzy entries must be [from, to] pairs");
      }
      cfg.fuzzy.emplace_back(pair[0].get<std::string>(),
                             pair[1].get<std::string>());
    }
  }

  if (j.contains("max_candidates")) {
    cfg.max_candidates = j.at("max_candidates").get<int>();
    if (cfg.max_candidates <= 0) {
      throw std::runtime_error("max_candidates must be positive");
    }
  }

  if (j.contains("segment_penalty")) {
    cfg.segment_penalty = j.at("segment_penalty").get<int>();
    if (cfg.segment_penalty < 0) {
      throw std::runtime_error("segment_penalty must be non-negative");
    }
  }

  if (j.contains("user_words")) {
    for (const auto& w : j.at("user_words")) {
      UserWord uw;
      uw.pinyin = w.at("pinyin").get<std::string>();
      uw.word = w.at("word").get<std::string>();
      uw.freq = w.value("freq", 0.0);
      cfg.user_words.push_back(std::move(uw));
    }
  }

  return cfg;
}

}  // namespace naive_pinyin
