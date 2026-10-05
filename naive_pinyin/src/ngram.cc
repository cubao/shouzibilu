#include "ngram.h"

#include <algorithm>
#include <cstring>

namespace naive_pinyin {

namespace {

constexpr size_t kHeaderBytes = 16;
constexpr size_t kKeyBytes = 8;
constexpr size_t kValueBytes = 4;
constexpr uint32_t kVersion = 1;
// 超过任何合理表的条目数：截断或碰巧带 magic 的外来文件不应变成离谱分配。
constexpr uint32_t kMaxEntries = 40 * 1000 * 1000;

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;

// 显式按小端拼装，不依赖宿主字节序。
uint32_t ReadU32LE(const char* p) {
  const auto* b = reinterpret_cast<const unsigned char*>(p);
  return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
         (static_cast<uint32_t>(b[2]) << 16) |
         (static_cast<uint32_t>(b[3]) << 24);
}

uint64_t ReadU64LE(const char* p) {
  uint64_t lo = ReadU32LE(p);
  uint64_t hi = ReadU32LE(p + 4);
  return lo | (hi << 32);
}

float ReadF32LE(const char* p) {
  const uint32_t bits = ReadU32LE(p);
  float v;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

}  // namespace

bool NgramTable::Load(const char* data, size_t size) {
  keys_.clear();
  values_.clear();
  if (!data || size < kHeaderBytes) return false;
  if (std::memcmp(data, "MSNG", 4) != 0) return false;
  if (ReadU32LE(data + 4) != kVersion) return false;
  const uint32_t count = ReadU32LE(data + 8);
  const size_t needed = kHeaderBytes +
                        static_cast<size_t>(count) * (kKeyBytes + kValueBytes);
  if (count > kMaxEntries || needed > size) return false;

  std::vector<uint64_t> keys;
  std::vector<float> values;
  keys.reserve(count);
  values.reserve(count);
  uint64_t previous = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t key =
        ReadU64LE(data + kHeaderBytes + static_cast<size_t>(i) * kKeyBytes);
    // 有序是查找正确性的前提：外来工具产出的无序表在这里拒绝。
    if (i > 0 && key < previous) return false;
    previous = key;
    keys.push_back(key);
  }
  const char* value_base =
      data + kHeaderBytes + static_cast<size_t>(count) * kKeyBytes;
  for (uint32_t i = 0; i < count; ++i) {
    values.push_back(ReadF32LE(value_base + static_cast<size_t>(i) * kValueBytes));
  }
  keys_ = std::move(keys);
  values_ = std::move(values);
  return true;
}

uint64_t NgramTable::HashWords(const std::string* words, int n) {
  uint64_t state = kFnvOffset;
  for (int i = 0; i < n; ++i) {
    if (i > 0) state *= kFnvPrime;  // 0x00 分隔：xor 0 无效，只剩乘法
    for (unsigned char c : words[i]) {
      state ^= static_cast<uint64_t>(c);
      state *= kFnvPrime;
    }
  }
  return state;
}

float NgramTable::Increment(const std::string* words, int n) const {
  if (keys_.empty() || n <= 0 || words[n - 1].empty()) return 0.0f;
  const uint64_t key = HashWords(words, n);
  const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
  if (it != keys_.end() && *it == key) return values_[it - keys_.begin()];
  return 0.0f;
}

}  // namespace naive_pinyin
