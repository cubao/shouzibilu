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
- **动态词**：`,check` → ✅、`,date` → 今天日期，`,` 开头的纯映射（`eval:` 可执行 JS）
- **组词光标**：方向键按音节边界（DAG 全边界）移动，逐段构词即学会新词
- **精简 vim**：Normal/Insert/Search 三模式，operator × motion / text-object（可关）
- **键盘布局无关**：物理键（e.code）+ 布局表（qwerty / dvorak / dvorak4tzx，JSON 可覆盖）
- **词库**：雾凇拼音 [rime-ice](https://github.com/iDvel/rime-ice)（简体、现代词频）
  + [rime-essay](https://github.com/rime/rime-essay) 单字频率表，离线转成紧凑文本格式

**明确不做**：简拼、编辑距离纠错、繁简切换、Lua、云词库。
（标点三种风格、Shift 中英切换 + 大写直通、动态调频、自造词学习均已实现。）

## 目录结构

```
index.html             浏览器 demo（仓库根，GitHub Pages 直接部署）
ime-editor.js          IME 编辑器（键盘接管 / 候选弹窗 / 动态词 / vim）
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
精简词库 + `ime-editor.js`（浏览器 IME 编辑器，Node 下可 require 不碰 DOM），
开箱即用。静态文件（package.json / index.js / index.d.ts /
README.md）在库中，构建产物由 `make npm` 拷入（gitignore）。

```js
const { createEngine, ziranma, imeEditor } = require("@cubao/naive-pinyin");
const engine = await createEngine({ shuangpin: ziranma.shuangpin });
engine.query("nihkuijx").candidates[0].text;   // => 你好世界
engine.segment("wodedkdp").boundaries;          // => [0,2,4,6,8] 组词光标站位
```

发布流程（需 npm 账号且属于 @cubao org）：

```sh
make npm          # 组装
make npm-publish  # pack dry-run 后发布到官方 registry（scoped 包显式 public）
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
make e2e      # 浏览器端到端冒烟（需 playwright + Chromium，缺依赖自动 SKIP）
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

浏览器 demo（`make demo` 后打开 http://localhost:8000/）。编辑器是 textarea，
键盘全部接管（vim 默认开，启动在 Normal；`i` 进入插入模式才激活输入法）。

**插入模式**：

| 键 | 行为 |
|---|---|
| 字母 / `'` | 组成编码（`'` 为音节分隔；`,` 先上屏字面逗号，紧跟小写字母才转动态词） |
| 空格 / 数字 1-9 | 选字（无候选时字母原样上屏） |
| `←` `→` / `Home` `End` | 组词光标按音节边界移动（候选只查光标前前缀） |
| 翻页键（可配，默认 `,` `.`） | 候选翻页（拼音缓冲内；空缓冲 `,` 是动态词） |
| 标点键 | 首选上屏并出标点 |
| Shift 单击 | 中/英文切换；Shift+字母 = 首选上屏 + 写大写字母 |
| Backspace | 删光标前一字母；Enter 原样上屏；Esc 清缓冲（再按回 Normal） |

**动态词**（纯映射不学习，中/英模式都生效）：`,` 先上屏字面逗号，
**紧跟**小写字母才删掉逗号进选择菜单（`,check` → ✅、`,date` → 日期、
`eval:` 值执行 JS）；跟任何其他键（空格/数字/标点/方向键）都无事发生——
正常打 `hello, world` 全程无感。模式内空格/回车 = 精确 > 首选 > 字面逗号。
页面底部「动态词映射」可编辑整表（保存时 eval 条目需确认）。

**普通模式**（精简 vim）：`h j k l` `0 ^ $` `w b e`（每汉字一词）`gg G` `%`
`f F t T` `/ ?`+`n N`；operator `d c y >`（`dd cc yy >> <<`），text object
`iw aw`、引号（含中文弯引号）、括号（含（）【】《》「」）；`x p P u`；
`r<char>` 替换单字符（`r<Enter>` 拆行）；`i a A I o O` 进插入。
Normal 模式显示 block 块光标（`mix-blend-mode: difference` 反色块，
字符仍由 textarea 渲染，字号字高不变）。
搜索遵循当前中/英状态：中文态可打拼音出候选选词，英文态字面输入；
incsearch 实时跳转 + 全文高亮（当前命中橙色），接受后高亮保留（`n`/`N` 导航），
Normal 下 Esc 清除高亮；搜索中 Esc 取消并还原光标。Esc 别名：`Ctrl+[`、`Ctrl+C`。
明确不做：`.`、Visual、宏、正则、计数。

**界面与持久化**：编辑器撑满窗口中部（flex 布局），带绝对行号 gutter
（折行对齐、滚动同步）；键盘驱动的光标移动自动最小滚动保持可见（scrolloff=0）。
编辑器用等宽字体（Maple Mono NF CN web font，离线时退化为本地合成的
Menlo+PingFang 等宽）；编辑器内容与动态词映射草稿每 10s 写入
localStorage（页面隐藏时冲刷），刷新不丢。

**已知边界**：OS 级中文输入法激活时按键会被系统吞掉，网页无法压制——
请把 OS 输入源切到英文状态使用；physical 模式屏蔽的是键盘布局，不是 OS 输入法。

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
