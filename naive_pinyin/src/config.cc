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

  if (j.contains("user_boost")) {
    const auto& ub = j.at("user_boost");
    cfg.user_boost.first = ub.value("first", cfg.user_boost.first);
    cfg.user_boost.inc = ub.value("inc", cfg.user_boost.inc);
    cfg.user_boost.max = ub.value("max", cfg.user_boost.max);
    if (cfg.user_boost.first < 0 || cfg.user_boost.inc < 0 ||
        cfg.user_boost.max < 0) {
      throw std::runtime_error("user_boost values must be non-negative");
    }
  }

  if (j.contains("user_freq")) {
    for (const auto& e : j.at("user_freq")) {
      UserFreqEntry ufe;
      ufe.pinyin = e.at("pinyin").get<std::string>();
      ufe.word = e.at("word").get<std::string>();
      ufe.count = e.value("count", 0);
      cfg.user_freq.push_back(std::move(ufe));
    }
  }

  if (j.contains("lattice")) {
    const auto& lt = j.at("lattice");
    if (lt.is_boolean()) {
      cfg.lattice.enabled = lt.get<bool>();
    } else if (lt.is_object()) {
      cfg.lattice.enabled = lt.value("enabled", cfg.lattice.enabled);
      cfg.lattice.beam = lt.value("beam", cfg.lattice.beam);
      cfg.lattice.nbest = lt.value("nbest", cfg.lattice.nbest);
      cfg.lattice.max_entries_per_edge =
          lt.value("max_entries_per_edge", cfg.lattice.max_entries_per_edge);
      cfg.lattice.max_alternates =
          lt.value("max_alternates", cfg.lattice.max_alternates);
      cfg.lattice.bigram_weight =
          lt.value("bigram_weight", cfg.lattice.bigram_weight);
      cfg.lattice.trigram_weight =
          lt.value("trigram_weight", cfg.lattice.trigram_weight);
      cfg.lattice.phrase_bonus = lt.value("phrase_bonus", cfg.lattice.phrase_bonus);
      cfg.lattice.unigram_char_span =
          lt.value("unigram_char_span", cfg.lattice.unigram_char_span);
      cfg.lattice.unigram_char_floor =
          lt.value("unigram_char_floor", cfg.lattice.unigram_char_floor);
      cfg.lattice.unigram_phrase_span =
          lt.value("unigram_phrase_span", cfg.lattice.unigram_phrase_span);
      cfg.lattice.personal_weight =
          lt.value("personal_weight", cfg.lattice.personal_weight);
      cfg.lattice.personal_max =
          lt.value("personal_max", cfg.lattice.personal_max);
      if (cfg.lattice.beam <= 0 || cfg.lattice.nbest <= 0 ||
          cfg.lattice.max_entries_per_edge <= 0 ||
          cfg.lattice.max_alternates < 0) {
        throw std::runtime_error(
            "lattice beam/nbest/max_entries_per_edge must be positive, "
            "max_alternates non-negative");
      }
    } else {
      throw std::runtime_error("lattice must be a boolean or an object");
    }
  }

  return cfg;
}

}  // namespace naive_pinyin
