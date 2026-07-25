# macOS 输入法（手自笔录）

基于 InputMethodKit 的原生 macOS 输入法，复用仓库同一份 `naive_pinyin` C++
核心（直接编进 App，不走 wasm），参考 `../squirrel`（鼠须管）的组织方式：
TIS 注册流程移植自 `sources/InputSource.swift`，主入口/命令行动作参考
`sources/Main.swift`。候选窗用系统自带 `IMKCandidates`（Squirrel 因主题化
需求自绘面板，这里保持最小实现）。

## 构建与安装

```sh
make macos          # 构建 build/macos/Shouzibilu.app（含内置词库，ad-hoc 签名）
make macos-install  # 装入 ~/Library/Input Methods 并注册 + 启用
```

安装后在 **系统设置 → 键盘 → 输入法** 中选中「手自笔录」（或从菜单栏
输入法菜单切换）。也可以尝试命令行选中（部分环境下系统会拒绝程序化
切换，报 err=-50，属正常现象，手动选即可）：

```sh
~/Library/Input\ Methods/Shouzibilu.app/Contents/MacOS/Shouzibilu --select-input-source
```

其它管理动作（无需先启动）：`--install` / `--enable-input-source` /
`--disable-input-source` / `--quit` / `--help`。

调试冒烟（不经 IMK 直接查核心）：

```sh
.../Shouzibilu --query nihao     # 打印候选 JSON
```

## 按键

| 按键 | 行为 |
|---|---|
| 字母 / `'` | 组码（`'` 为音节分隔符） |
| 空格 / Tab | 上屏高亮候选 |
| 数字 1-9 | 上屏候选窗可见行 |
| ↑ / ↓ | 移动高亮 |
| ← / → / PgUp / PgDn / `-` `=` `[` `]` | 翻页 |
| 回车 | 原文上屏；Esc 清除；退格删码 |
| Shift 单击 | 切换中/英文 |
| Shift+字母 | 原文上屏已有输入，大写字母透给应用 |
| 标点 | 顶上屏首选，标点透给应用 |
| Cmd/Ctrl/Opt 组合键 | 一律透给应用 |

## 配置与词表（与 web/npm 版同格式）

用户目录 `~/Library/Application Support/Shouzibilu/`（输入法菜单里有
「打开用户目录…」）：

- `config.json` — 可选，核心配置，格式同 npm 包：
  ```json
  {"shuangpin": {"map": {"aa": "a", ...}}, "fuzzy": [["z", "zh"]], "max_candidates": 50}
  ```
  双拼可直接拷内置的 `ziranma.json`（`.app/Contents/Resources/` 或仓库
  `wasm/` 下）为 `config.json`。
- `*.dict.txt` — 可选，追加用户词表，格式同 `data/naive_pinyin.dict.txt`
  （`拼 音 键<TAB>词:分,词:分`），后加载可提频。
- `user_freq.json` — 动态调频叠加层，自动维护，无需手改。

改动配置/词表后，用输入法菜单的「重新加载词库与配置…」生效。

## 文件

```
macos/Info.plist               输入法 bundle 元数据（TIS 声明）
macos/Sources/main.mm          入口：IMKServer 主循环 / 一次性管理动作
macos/Sources/AppDelegate.*    引擎单例、中英文状态、定时回写用户调频
macos/Sources/Engine.*         naive_pinyin C API 封装（配置合并、词表加载、JSON）
macos/Sources/InputController.mm  IMKInputController：按键 -> 组字 -> 候选 -> 上屏
macos/Sources/Installer.*      TIS 注册/启用/选中（移植自 Squirrel）
```

无 Xcode 工程：全部经根 Makefile 以 `clang++ -fobjc-arc` 构建。
