// naive_pinyin 命令行查询工具（native 调试用）
//
// 用法:
//   cli <dict文件> [config.json]           进入 REPL
//   cli <dict文件> [config.json] <输入...>  直接查询
//
// 例:
//   ./build/native/cli data/naive_pinyin.dict.txt '{}' nihaoshijie
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <naive_pinyin/naive_pinyin.h>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <dict> [config.json] [input...]\n",
                 argv[0]);
    return 1;
  }
  const char* dict_path = argv[1];
  std::string config = argc >= 3 ? argv[2] : "{}";

  std::ifstream in(dict_path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "cannot open dict: %s\n", dict_path);
    return 1;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string dict_data = ss.str();

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

  if (argc >= 4) {
    for (int i = 3; i < argc; ++i) {
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
