#include <naive_pinyin/c_api.h>

#include <string>

#include <naive_pinyin/naive_pinyin.h>

#include "engine.h"

using naive_pinyin::Engine;
using naive_pinyin::EngineImpl;

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

const char* np_segment(void* ctx, const char* input) {
  if (!ctx || !input) return nullptr;
  static std::string result;
  result = static_cast<Engine*>(ctx)->Segment(input);
  return result.c_str();
}

void np_commit(void* ctx, const char* segments_json) {
  if (!ctx || !segments_json) return;
  static_cast<EngineImpl*>(ctx)->Commit(segments_json);
}

void np_learn_word(void* ctx, const char* key, const char* word) {
  if (!ctx || !key || !word) return;
  static_cast<EngineImpl*>(ctx)->LearnWord(key, word);
}

const char* np_dump_user(void* ctx) {
  if (!ctx) return nullptr;
  static std::string result;
  result = static_cast<EngineImpl*>(ctx)->DumpUser();
  return result.c_str();
}

void np_destroy(void* ctx) { delete static_cast<Engine*>(ctx); }
