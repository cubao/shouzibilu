// 拼音音节表与切分（全拼 syllabifier）
#pragma once

#include <string>
#include <vector>

namespace naive_pinyin {

// 是否合法的无声调拼音音节（含 biang/cei/lo/lve/nve 等口语音节）。
bool IsSyllable(const std::string& s);

// 把音节拆成 (声母, 韵母)。零声母时 initial 为空。
// 如 "zhuang" -> ("zh", "uang")，"an" -> ("", "an")。
void SplitSyllable(const std::string& syl, std::string* initial,
                   std::string* final_);

// 一条音节边：raw input 的 [start, end) 构成一个合法音节。
struct SyllableEdge {
  int start;
  int end;
  std::string syllable;
};

// 切分结果：字母流上的音节 DAG。
// 节点为 raw input 的下标 0..length；apostrophe(') 位置是硬边界。
struct Segmentation {
  int length = 0;
  // edges_from[i]: 从 raw 下标 i 出发的音节边（不跨 apostrophe）。
  std::vector<std::vector<SyllableEdge>> edges_from;
  // boundary[i] == true 表示 input[i] 是 apostrophe。
  std::vector<bool> boundary;
};

// 全拼切分：枚举所有合法音节边（含 apostrophe 硬边界处理）。
Segmentation SegmentFullPinyin(const std::string& input);

}  // namespace naive_pinyin
