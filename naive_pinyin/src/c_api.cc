#include <naive_pinyin/c_api.h>

#include <string>

#include <naive_pinyin/naive_pinyin.h>

using naive_pinyin::Engine;

void* np_create(const char* config_json) {
  if (!config_json) return nullptr;
  auto engine = Engine::CreateFromJson(config_json);
  return engine.release();
}

int np_load_dict(void* ctx, const char* buf, int len) {
  if (!ctx || !buf || len <= 0) return 0;
  return static_cast<Engine*>(ctx)->LoadDict(buf, static_cast<size_t>(len))
             ? 1
             : 0;
}

const char* np_query(void* ctx, const char* input) {
  if (!ctx || !input) return nullptr;
  // wasm 单线程，JS 侧调用后立即拷贝，静态缓冲区安全。
  static std::string result;
  result = static_cast<Engine*>(ctx)->Query(input);
  return result.c_str();
}

void np_destroy(void* ctx) { delete static_cast<Engine*>(ctx); }
