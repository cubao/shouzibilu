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

// 销毁引擎实例。
void np_destroy(void* ctx);

#ifdef __cplusplus
}  // extern "C"
#endif
