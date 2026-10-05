// naive_pinyin 命令行查询工具（native 调试用）
//
// 用法:
//   cli [--lm <bigram文件>] [--trigram <trigram文件>]
//       <dict文件> [config.json]           进入 REPL
//   cli [...] <dict文件> [config.json] <输入...>  直接查询
//
// 例:
//   ./build/native/cli data/naive_pinyin.dict.txt '{}' nihaoshijie
//   ./build/native/cli --lm data/msime-bigram.bin \
//       data/naive_pinyin.dict.txt '{}' nihaoshijie
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <naive_pinyin/naive_pinyin.h>

#include "../naive_pinyin/src/engine.h"

namespace {

std::string ReadFile(const char* path, const char* what) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open %s: %s\n", what, path);
    std::exit(1);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::string lm_path, trigram_path;
  int positional = 0;
  const char* dict_path = nullptr;
  std::string config;
  bool config_seen = false;
  std::vector<int> inputs_idx;  // argv 下标

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--lm" && i + 1 < argc) {
      lm_path = argv[++i];
    } else if (arg == "--trigram" && i + 1 < argc) {
      trigram_path = argv[++i];
    } else if (positional == 0) {
      dict_path = argv[i];
      ++positional;
    } else if (positional == 1 && !config_seen) {
      config = argv[i];
      config_seen = true;
      ++positional;
    } else {
      inputs_idx.push_back(i);
    }
  }
  if (!dict_path) {
    std::fprintf(stderr, "usage: %s [--lm <bigram>] [--trigram <trigram>] "
                         "<dict> [config.json] [input...]\n",
                 argv[0]);
    return 1;
  }

  std::string dict_data = ReadFile(dict_path, "dict");

  std::string err;
  auto engine = naive_pinyin::Engine::CreateFromJson(config, &err);
  if (!engine) {
    std::fprintf(stderr, "config error: %s\n", err.c_str());
    return 1;
  }
  if (!engine->LoadDict(dict_data.data(), dict_data.size())) {
    std::fprintf(stderr, "dict load failed\n");
    return 1;
  }
  std::fprintf(stderr, "dict loaded: %.1f MB\n",
               dict_data.size() / 1024.0 / 1024.0);

  auto* impl = static_cast<naive_pinyin::EngineImpl*>(engine.get());
  if (!lm_path.empty()) {
    std::string lm = ReadFile(lm_path.c_str(), "bigram");
    if (!impl->LoadLm(lm.data(), lm.size())) {
      std::fprintf(stderr, "bigram load failed\n");
      return 1;
    }
    std::fprintf(stderr, "bigram loaded: %.1f MB\n",
                 lm.size() / 1024.0 / 1024.0);
  }
  if (!trigram_path.empty()) {
    std::string lm = ReadFile(trigram_path.c_str(), "trigram");
    if (!impl->LoadTrigram(lm.data(), lm.size())) {
      std::fprintf(stderr, "trigram load failed\n");
      return 1;
    }
    std::fprintf(stderr, "trigram loaded: %.1f MB\n",
                 lm.size() / 1024.0 / 1024.0);
  }

  if (!inputs_idx.empty()) {
    for (int i : inputs_idx) {
      std::printf("%s => %s\n", argv[i], engine->Query(argv[i]).c_str());
    }
    return 0;
  }

  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    if (line.empty()) continue;
    std::cout << engine->Query(line) << "\n";
  }
  return 0;
}
