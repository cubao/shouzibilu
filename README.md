# 手自笔录 (shǒuzìbǐlù)

> 家贫，无从致书以观，每假借于藏书之家，手自笔录，计日以还。
> —— 明·宋濂《送东阳马生序》

AI 时代，留一个独立的中文输入环境，手写一点文字。

## 这是什么

从 [librime](https://github.com/rime/librime) 提取最核心的「字母串 → 汉字候选」能力，
用 C++17 + STL 重写的精简库 `naive_pinyin`，编译到 WebAssembly，
在浏览器网页内提供一个完全自定义的中文输入法。

- **全拼 + 双拼**（默认自然码，映射表走 JSON 配置，任意方案可配）
- **整句输入**：自动音节切分 + DP 选最优路径
- **模糊音**：可配置的模糊音对（zh/z、n/l、in/ing……）
- **自定义词表**：JSON 配置追加用户词条
- **词库**：雾凇拼音 [rime-ice](https://github.com/iDvel/rime-ice)（简体、现代词频）
  + [rime-essay](https://github.com/rime/rime-essay) 单字频率表，离线转成紧凑文本格式

**明确不做**：简拼、编辑距离纠错、繁简切换、标点处理、英文混输、Lua。
动态调频（越打越准）v1 不做，架构预留，v2 再加。

## 目录结构

```
naive_pinyin/          C++17 库本体
  include/naive_pinyin/  对外头文件（C++ API + C API）
  src/                   实现
third_party/nlohmann/  json.hpp（header-only，拷自 nlohmann/json）
tools/                 Python 离线工具（词典转换等，不进 wasm 依赖链）
tests/                 native 单元测试（自带轻量框架，无 gtest）
wasm/                  wasm 产物与 JS glue、node 冒烟测试
demo/                  浏览器演示页
data/                  生成的词典（gitignore，make dict 生成）
```

## 常用命令

```sh
make test     # 构建并运行 native 单元测试（日常开发主用）
make native   # 只构建
make cli      # 命令行查询工具（native 调试）
make dict     # 生成精简词典（依赖 ../rime-ice）
make ziranma  # 生成自然码双拼默认配置 wasm/ziranma.json
make wasm     # 编译 WebAssembly（先 source ../emsdk/emsdk_env.sh）
make smoke    # node 冒烟测试 wasm 产物
make demo     # 起本地服务，打开 http://localhost:8000/demo/
make clean
```

## 依赖

| 依赖 | 位置 | 用途 |
|---|---|---|
| C++17 编译器 | 系统 | native 构建与测试 |
| python3 | 系统 | 离线词典工具 |
| emsdk | `../emsdk` | wasm 构建 |
| librime | `../librime` | 算法蓝本（不链接） |
| nlohmann/json | `../json` | 已拷入 `third_party/` |
| rime-ice | `../rime-ice` | 词库源（离线处理） |
| rime-essay | `../rime-essay` | 单字频率表（离线处理） |

## wasm 接口（C API + JSON 字符串）

```c
void*      np_create(const char* config_json);      // 创建引擎
int        np_load_dict(void* ctx, const char* buf, int len);
const char* np_query(void* ctx, const char* input); // 返回 JSON 候选串
void       np_destroy(void* ctx);
```

配置示例：

```json
{
  "shuangpin": {"map": {"ni": "ni", "hk": "hao", "...": "..."}},
  "fuzzy": [["z", "zh"], ["c", "ch"], ["s", "sh"], ["n", "l"], ["in", "ing"]],
  "max_candidates": 10,
  "user_words": [{"pinyin": "na yi ge", "word": "那一个", "freq": 100}]
}
```

`shuangpin` 缺省即为全拼模式。自然码映射表由 `tools/gen_ziranma.py` 生成。

## 快速体验

```sh
make dict cli
./build/native/cli data/naive_pinyin.dict.txt '{}' nihaoshijie "xi'an"
# 或不带查询词进入 REPL：
./build/native/cli data/naive_pinyin.dict.txt '{"fuzzy":[["z","zh"],["in","ing"]]}'
```

## 设计原则

1. 尽量只用 STL；外部依赖一律 header-only 且拷入 `third_party/`
2. 配置一律 JSON；不要 yaml/toml
3. C++ 越笨越好：双拼是纯查表，复杂性推到配置侧
4. 词典只存全拼；双拼、模糊音全部运行时展开
5. 小步迭代：native 单测验证算法，wasm 只做薄薄一层绑定

## 致谢

- [librime / Rime 输入法](https://github.com/rime/librime) —— 算法蓝本（音节切分、DP 整句匹配）
- [雾凇拼音 rime-ice](https://github.com/iDvel/rime-ice) —— 词库
- [rime-essay](https://github.com/rime/rime-essay) —— 单字频率表
- [nlohmann/json](https://github.com/nlohmann/json) —— JSON 解析
