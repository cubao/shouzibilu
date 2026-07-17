// 词典：音节 key 的 trie + 候选列表
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace naive_pinyin {

struct DictEntry {
  std::string word;
  int score = 0;  // 0..1000，越高越常用
};

// 紧凑文本格式（每行）：
//   空格分隔拼音<TAB>词:分数,词:分数,...
// 如 "ni hao\t你好:758,尼号:490"
class Dict {
 public:
  struct Node {
    std::unordered_map<std::string, std::unique_ptr<Node>> children;
    std::vector<DictEntry> entries;  // 按分数降序
  };

  bool Load(const char* data, size_t size);

  // 添加/更新一个词条（用户自定义词）。key 为空格分隔音节串。
  void AddEntry(const std::string& key, const std::string& word, int score);

  const Node* root() const { return &root_; }
  int max_key_length() const { return max_key_length_; }
  size_t num_entries() const { return num_entries_; }

 private:
  Node root_;
  int max_key_length_ = 1;
  size_t num_entries_ = 0;
};

}  // namespace naive_pinyin
