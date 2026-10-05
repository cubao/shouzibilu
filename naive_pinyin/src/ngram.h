// n-gram 语言模型增量表：bigram / trigram 共用的只读排序表。
//
// 二进制格式（"MSNG" v1，小端，与 msime 表文件字节兼容）：
//   magic "MSNG"(4B) + version u32(=1) + count u32 + reserved u32(=0)
//   count × u64 键（升序，允许重复键，查找取第一条）
//   count × f32 值（与键一一对应）
// 每条 12 字节，100 万条约 12MB。
//
// 键 = FNV-1a 64（UTF-8 字节）：词1 + 0x00 + 词2（trigram 再 + 0x00 + 词3）。
// 值 = ln(P(next|prev) / P(next)) 增量：同现率高于独立概率为正、低于为负，
//      缺失记 0（不奖不罚），值域 clamp ±3。句首前词用 kSentenceStart。
//
// 表设计与查询语义移植自 msime（metasequoiaime/msime，GPL-3.0）
// crates/engine/src/lattice/ngram.rs。msime 用 mmap + 16 位分桶索引降缓存行
// 跳变，这里 wasm/桌面统一为整表读入内存 + 全表二分（1M 条约 20 次比较），
// wasm 无 mmap，桌面表尺寸下收益可忽略。
//
// 随表分发见 tools/subset_ngram.py 与 README「数据来源」：msime 官方表由
// 中文维基百科 dump（CC-BY-SA 4.0）统计而来，再分发须保留署名。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace naive_pinyin {

// 词序列统计里的句首标记：不是任何词条会包含的字符。
inline constexpr const char* kNgramSentenceStart = "\x01";

class NgramTable {
 public:
  NgramTable() = default;

  // 加载后独占内容，禁止拷贝（100 万条 12MB，误拷贝代价大）。
  NgramTable(const NgramTable&) = delete;
  NgramTable& operator=(const NgramTable&) = delete;

  // 从内存缓冲区加载。校验 magic/版本/大小/键有序，失败返回 false
  // 且内容被清空（加载失败 = 引擎退回无语言模型解码，不报错）。
  bool Load(const char* data, size_t size);

  // words 按序给出（bigram 2 个、trigram 3 个），返回增量；缺失或
  // 末词为空返回 0。
  float Increment(const std::string* words, int n) const;

  bool empty() const { return keys_.empty(); }
  size_t size() const { return keys_.size(); }

  // FNV-1a 64：词间以单个 0x00 分隔（xor 0 为无效操作，只多乘一次素数），
  // 无尾随分隔。与 msime fnv1a_words 及 tools/subset_ngram.py 保持一致。
  static uint64_t HashWords(const std::string* words, int n);

 private:
  std::vector<uint64_t> keys_;
  std::vector<float> values_;
};

}  // namespace naive_pinyin
