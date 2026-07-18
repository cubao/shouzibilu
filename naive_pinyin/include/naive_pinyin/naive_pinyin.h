// naive_pinyin - 字母串到汉字候选的精简引擎
// 手自笔录 (shouzibilu) 项目核心库
//
// Distributed under the BSD License.
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace naive_pinyin {

// 引擎：把字母序列（全拼或双拼）转换为汉字候选。
// 非线程安全；每个输入上下文创建一个实例。
class Engine {
 public:
  virtual ~Engine() = default;

  // 从 JSON 配置字符串创建引擎。
  // 配置解析失败时返回 nullptr，并填充 error（若非空）。
  //
  // 配置示例：
  //   {
  //     "shuangpin": {"map": {"ni": "ni", "hk": "hao", ...}},  // 缺省为全拼
  //     "fuzzy": [["z", "zh"], ["n", "l"], ["in", "ing"]],
  //     "max_candidates": 10,
  //     "user_words": [{"pinyin": "na yi ge", "word": "那一个", "freq": 100}]
  //   }
  static std::unique_ptr<Engine> CreateFromJson(const std::string& config_json,
                                                std::string* error = nullptr);

  // 从内存缓冲区加载紧凑文本格式词典。失败返回 false。
  virtual bool LoadDict(const char* data, size_t size) = 0;

  // 查询输入字母串的候选，返回 JSON 数组字符串。
  // 例如 [{"text":"你好","weight":100.0,"consumed":5}, ...]
  // 输入含非法字符时返回带 "error" 字段的 JSON 对象。
  virtual std::string Query(const std::string& input) const = 0;

  // 返回输入字母串的音节边界（组词光标站位），JSON 形如
  // {"input":"wodedkdp","boundaries":[0,2,4,6,8]}。
  // 边界 = 从起点 0 经音节边可达的所有位置，外加末尾（半截音节
  // 不算边界，但末尾本身永远是一站）。apostrophe 是硬边界。
  // 输入含非法字符时返回带 "error" 字段的 JSON 对象。
  virtual std::string Segment(const std::string& input) const = 0;
};

}  // namespace naive_pinyin
