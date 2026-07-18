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
index.html             浏览器 demo（仓库根，GitHub Pages 直接部署）
naive_pinyin/          C++17 库本体
  include/naive_pinyin/  对外头文件（C++ API + C API）
  src/                   实现
third_party/nlohmann/  json.hpp（header-only，拷自 nlohmann/json）
tools/                 Python 离线工具（词典转换等，不进 wasm 依赖链）
tests/                 native 单元测试（自带轻量框架，无 gtest）
wasm/                  wasm 产物（已入库供 Pages 部署）与 JS glue、node 冒烟测试
data/                  生成的词典（naive_pinyin.dict.txt 已入库；jieba 源文件 gitignore）
```

## npm 包（@cubao/naive-pinyin）

`npm/` 目录即 npm 包：薄 JS 封装（隐藏 ccall 样板）+ wasm + 自然码码表 +
精简词库，开箱即用。静态文件（package.json / index.js / index.d.ts /
README.md）在库中，构建产物由 `make npm` 拷入（gitignore）。

```js
const { createEngine, ziranma } = require("@cubao/naive-pinyin");
const engine = await createEngine({ shuangpin: ziranma.shuangpin });
engine.query("nihkuijx").candidates[0].text;  // => 你好世界
```

发布流程（需 npm 账号且属于 @cubao org）：

```sh
make npm                              # 组装
cd npm && npm publish --access public # 发布（scoped 包默认私有，需显式 public）
```

## 部署（GitHub Pages）

wasm 产物与词典已入库，`index.html` 在仓库根。仓库设置 → Pages →
Deploy from a branch → 选 `dev` 分支 / (root) 即可，访问
`https://district10.github.io/shouzibilu/`。
本地预览与线上同构：`make demo` 后打开 http://localhost:8000/。

## 常用命令

```sh
make test     # 构建并运行 native 单元测试（日常开发主用）
make native   # 只构建
make cli      # 命令行查询工具（native 调试）
make dict     # 生成精简词典（依赖 ../rime-ice）
make ziranma  # 生成自然码双拼默认配置 wasm/ziranma.json
make wasm     # 编译 WebAssembly（先 source ../emsdk/emsdk_env.sh）
make smoke    # node 冒烟测试 wasm 产物
make demo     # 起本地服务，打开 http://localhost:8000/
make regression  # 排序质量回归（21 条断言）
make npm      # 组装 npm 包到 npm/（@cubao/naive-pinyin）
make npm-test # 组装并自测 npm 包封装
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

浏览器 demo（`make demo` 后打开 http://localhost:8000/demo/）按键：

| 键 | 行为 |
|---|---|
| 字母 / `'` | 组成编码（`'` 为音节分隔；无缓冲时是引号） |
| 空格 / 数字 1-9 | 选字（无候选时字母原样上屏） |
| 翻页键（可配，默认 `,` `.`，备选 `-` `=` / `[` `]` / PgUp/PgDn） | 候选翻页（每页 9 个，共 50 候选） |
| 标点键 | 首选上屏并出标点（三种符号风格可选：英文/中文/繁体） |
| Shift 单击 | 中/英文模式切换 |
| Backspace | 删缓冲字母（无缓冲时默认删除） |
| Enter | 字母原样上屏（无缓冲时默认换行） |
| Esc | 清空缓冲 |

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
