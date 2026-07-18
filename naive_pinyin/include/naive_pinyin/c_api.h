// naive_pinyin C API - wasm / JS 边界
//
// Distributed under the BSD License.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 从 JSON 配置创建引擎实例。失败返回 NULL。
void* np_create(const char* config_json);

// 加载词典缓冲区。成功返回 1，失败返回 0。
int np_load_dict(void* ctx, const char* buf, int len);

// 查询候选。返回 JSON 字符串指针（引擎内部静态缓冲区，
// 下次调用即失效，调用方需立即拷贝）。ctx 非法时返回 NULL。
const char* np_query(void* ctx, const char* input);

// 音节边界（组词光标站位）。返回 JSON 形如
// {"input":"wodedkdp","boundaries":[0,2,4,6,8]}，
// 静态缓冲区，下次调用即失效。
const char* np_segment(void* ctx, const char* input);

// 提交一个候选的分段（动态调频）。segments_json 形如
// [{"key":"ni hao","word":"你好"}, ...]。
void np_commit(void* ctx, const char* segments_json);

// 学习自造词：key 为空格分隔音节（如 "tang zhi xiong"）。
void np_learn_word(void* ctx, const char* key, const char* word);

// 导出用户叠加层 JSON（[{"pinyin":"...","word":"...","count":n}]），
// 供 localStorage 持久化/导出。返回静态缓冲区，下次调用即失效。
const char* np_dump_user(void* ctx);

// 销毁引擎实例。
void np_destroy(void* ctx);

#ifdef __cplusplus
}  // extern "C"
#endif
