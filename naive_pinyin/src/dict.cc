#include "dict.h"

#include <algorithm>
#include <cstring>

namespace naive_pinyin {

namespace {

// 按空格切分音节 key。
std::vector<std::string> SplitKey(const char* begin, const char* end) {
  std::vector<std::string> syllables;
  const char* p = begin;
  while (p < end) {
    while (p < end && *p == ' ') ++p;
    const char* s = p;
    while (p < end && *p != ' ') ++p;
    if (p > s) syllables.emplace_back(s, p - s);
  }
  return syllables;
}

}  // namespace

void Dict::AddEntry(const std::string& key, const std::string& word,
                    int score) {
  std::vector<std::string> syllables = SplitKey(
      key.data(), key.data() + key.size());
  if (syllables.empty()) return;

  Node* node = &root_;
  for (const std::string& syl : syllables) {
    auto& child = node->children[syl];
    if (!child) child = std::make_unique<Node>();
    node = child.get();
  }
  if (static_cast<int>(syllables.size()) > max_key_length_) {
    max_key_length_ = static_cast<int>(syllables.size());
  }

  // 已存在则更新分数，保持降序。
  auto& entries = node->entries;
  for (auto& e : entries) {
    if (e.word == word) {
      if (score > e.score) {
        e.score = score;
        std::sort(entries.begin(), entries.end(),
                  [](const DictEntry& a, const DictEntry& b) {
                    return a.score > b.score;
                  });
      }
      return;
    }
  }
  entries.push_back({word, score});
  std::sort(entries.begin(), entries.end(),
            [](const DictEntry& a, const DictEntry& b) {
              return a.score > b.score;
            });
  ++num_entries_;
}

bool Dict::Load(const char* data, size_t size) {
  if (!data || size == 0) return false;

  const char* p = data;
  const char* end = data + size;
  while (p < end) {
    const char* line_end = static_cast<const char*>(
        std::memchr(p, '\n', end - p));
    if (!line_end) line_end = end;

    // 跳过注释与空行
    if (p == line_end || *p == '#') {
      p = line_end + 1;
      continue;
    }

    const char* tab = static_cast<const char*>(
        std::memchr(p, '\t', line_end - p));
    if (!tab) {
      p = line_end + 1;
      continue;  // 格式错误的行静默跳过
    }

    std::vector<std::string> syllables = SplitKey(p, tab);
    if (!syllables.empty()) {
      Node* node = &root_;
      for (const std::string& syl : syllables) {
        auto& child = node->children[syl];
        if (!child) child = std::make_unique<Node>();
        node = child.get();
      }
      if (static_cast<int>(syllables.size()) > max_key_length_) {
        max_key_length_ = static_cast<int>(syllables.size());
      }

      // 解析 词:分数,词:分数,...
      auto& entries = node->entries;
      const char* q = tab + 1;
      while (q < line_end) {
        const char* colon = static_cast<const char*>(
            std::memchr(q, ':', line_end - q));
        if (!colon) break;
        const char* comma = static_cast<const char*>(
            std::memchr(colon, ',', line_end - colon));
        if (!comma) comma = line_end;
        int score = 0;
        for (const char* d = colon + 1; d < comma; ++d) {
          if (*d >= '0' && *d <= '9') score = score * 10 + (*d - '0');
        }
        entries.push_back({std::string(q, colon - q), score});
        ++num_entries_;
        q = comma + 1;
      }
      // 文件内已按分数降序生成，但保险起见再排一次。
      std::sort(entries.begin(), entries.end(),
                [](const DictEntry& a, const DictEntry& b) {
                  return a.score > b.score;
                });
    }
    p = line_end + 1;
  }
  return num_entries_ > 0;
}

}  // namespace naive_pinyin
