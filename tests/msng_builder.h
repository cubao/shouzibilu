// 测试用 MSNG 表构建器：与被测实现分头实现同一格式契约
//（格式定义见 naive_pinyin/src/ngram.h），防止两侧一起漂移。
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace msng_test_util {

inline uint64_t FnvJoin(const std::string& joined) {
  uint64_t digest = 0xCBF29CE484222325ULL;
  for (unsigned char c : joined) {
    digest ^= static_cast<uint64_t>(c);
    digest *= 0x100000001B3ULL;
  }
  return digest;
}

inline uint64_t HashPair(const std::string& a, const std::string& b) {
  return FnvJoin(a + '\0' + b);
}

inline uint64_t HashTriple(const std::string& a, const std::string& b,
                           const std::string& c) {
  return FnvJoin(a + '\0' + b + '\0' + c);
}

inline void AppendU32(std::string* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

inline void AppendU64(std::string* out, uint64_t v) {
  for (int i = 0; i < 8; ++i) out->push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

inline void AppendF32(std::string* out, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  AppendU32(out, bits);
}

// entries 键值对 → 合法的 MSNG v1 表字节（键升序化）。
inline std::string BuildTable(std::vector<std::pair<uint64_t, float>> entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const auto& a, const auto& b) { return a.first < b.first; });
  std::string out;
  out += "MSNG";
  AppendU32(&out, 1);
  AppendU32(&out, static_cast<uint32_t>(entries.size()));
  AppendU32(&out, 0);
  for (const auto& e : entries) AppendU64(&out, e.first);
  for (const auto& e : entries) AppendF32(&out, e.second);
  return out;
}

}  // namespace msng_test_util
