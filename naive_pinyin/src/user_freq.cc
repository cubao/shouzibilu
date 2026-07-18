#include "user_freq.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace naive_pinyin {

namespace {
std::string Encode(const std::string& key, const std::string& word) {
  return key + "\t" + word;
}
}  // namespace

void UserFreq::Commit(const std::string& key, const std::string& word) {
  ++counts_[Encode(key, word)];
}

int UserFreq::Count(const std::string& key, const std::string& word) const {
  auto it = counts_.find(Encode(key, word));
  return it == counts_.end() ? 0 : it->second;
}

int UserFreq::Boost(const std::string& key, const std::string& word) const {
  int count = Count(key, word);
  if (count <= 0) return 0;
  int boost = params_.first + (count - 1) * params_.inc;
  return std::min(boost, params_.max);
}

void UserFreq::Load(const std::vector<UserFreqEntry>& entries) {
  for (const auto& e : entries) {
    if (e.count > 0) counts_[Encode(e.pinyin, e.word)] = e.count;
  }
}

std::string UserFreq::Dump() const {
  nlohmann::json out = nlohmann::json::array();
  for (const auto& [encoded, count] : counts_) {
    size_t tab = encoded.find('\t');
    out.push_back({
        {"pinyin", encoded.substr(0, tab)},
        {"word", encoded.substr(tab + 1)},
        {"count", count},
    });
  }
  return out.dump();
}

}  // namespace naive_pinyin
