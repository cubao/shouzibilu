# macOS 输入法（手自笔录）

基于 InputMethodKit 的原生 macOS 输入法，复用仓库同一份 `naive_pinyin` C++
核心（直接编进 App，不走 wasm），参考 `../squirrel`（鼠须管）的组织方式：
TIS 注册流程移植自 `sources/InputSource.swift`，主入口/命令行动作参考
`sources/Main.swift`。候选窗自绘（`CandidatePanel`，5 列网格）——系统自带
`IMKCandidates` 在 macOS 26 上候选文字完全不渲染（改字体/前景/背景色
均无效），已弃用。

## 构建与安装

```sh
make macos          # 构建 build/macos/Shouzibilu.app（含内置词库，ad-hoc 签名）
make macos-install  # 装入 ~/Library/Input Methods 并注册 + 启用
```

安装后在 **系统设置 → 键盘 → 输入法** 中选中「手自笔录」（或从菜单栏
输入法菜单切换）。也可以尝试命令行选中（macOS 26 禁止程序化切换，
报 err=-50，属正常现象，手动选即可）：

```sh
~/Library/Input\ Methods/Shouzibilu.app/Contents/MacOS/Shouzibilu --select-input-source
```

其它管理动作（无需先启动）：`--install` / `--enable-input-source` /
`--disable-input-source` / `--quit` / `--help`。

调试冒烟（不经 IMK 直接查核心）：

```sh
.../Shouzibilu --query nihk      # 双拼（自然码）：你好
```

## 按键

| 按键 | 行为 |
|---|---|
| 字母 / `'` | 组码（`'` 为音节分隔符） |
| 空格 / Tab / `1` | 上屏高亮行第 1 列 |
| `2` `3` `8` `9` | 上屏高亮行第 2-5 列（挑的好按的键位） |
| ↑ / ↓ / `,` / `.` | 高亮行逐行移动（整行蓝底，窗口自动滚动） |
| ← / → | 组词光标按音节边界移动（查询只取光标前缀，供逐字/词确认） |
| Home / End | 组词光标到首 / 末音节边界 |
| PgUp / PgDn / `-` `=` `[` `]` | 翻页（±4 行） |
| 回车 | 原文上屏；Esc 清除；退格删码（并中止学习会话） |
| Shift 单击 | 切换中/英文 |
| Shift+字母 | 原文上屏已有输入，大写字母透给应用 |
| 空缓冲 `,` | 上屏「，」（动态词触发默认关闭，见下） |
| 标点 `，。？！；：（）【】《》` | 组字时顶上屏高亮行首列，再插对应中文标点 |
| Cmd/Ctrl/Opt 组合键 | 一律透给应用 |

候选窗：5 列 × 4 行自绘网格，跟随光标（行矩形定位）；光标行距屏幕
底边不足一个面板高时自动翻转到行上方，不遮挡文本。4 5 6 7 0 在
组字时不作选词键（吞掉）。

## 学习新词（与 web 版同语义）

连续**分次上屏**拼出的词会自动学习：比如词库没有「手自笔录」，打全码后
先选「手」、余码续组再选「自」、再选「笔录」——组字完成时整词
`np_learn_word("shou zi bi lu", "手自笔录")`，下次打全码直接出。

规则（`ime-editor.js` 的 sessionAccum/finishSession 同款）：

- 同一段组字里**上屏 ≥ 2 次**、拼出 **2-8 字**才学习（一次上屏 = 词库
  已有，只做调频）。
- 回车原文上屏 / 上屏动态词 / Esc / 退格会中止学习会话。
- 逐字/逐词确认：用 ← → 把组词光标左移到目标边界（候选随即只按
  光标前的码查询），选字后继续，与 web 版操作一致。
- 学习结果写入 `user_freq.json`（15 秒定时回写），重启后经
  `config.user_freq` 回灌词典，不丢。
- 想手动加词：在配置目录放 `*.dict.txt`（格式同
  `data/naive_pinyin.dict.txt`），菜单「重新加载词库与配置」生效。

## 动态词（`,` 开头触发，默认关闭）

**默认关闭**：空缓冲按 `,` 直接上屏「，」。要启用，在 `config.json`
里加 `"dynamic_comma": true`，菜单「重新加载词库与配置」生效。
启用后空缓冲按 `,` 进入动态词模式：`,check` → ✅。精确匹配优先，
其余按 key 字典序；空格上屏首选，无匹配时输出「，」+已输字母。

映射表：`~/Library/Application Support/Shouzibilu/mappings.json`
（首次启动从内置默认表 `macos/mappings.json` 拷贝，可手动改）。
`eval:` 仅内置支持 `date` / `time` / `datetime` / `uuid` 四项
（web 版的任意 JS eval 在原生端不可用，其它 eval 项忽略）。

## 配置与词表（与 web/npm 版同格式）

用户目录 `~/Library/Application Support/Shouzibilu/`（输入法菜单
「打开配置目录」直达）：

- `config.json` — 可选，核心配置，格式同 npm 包：
  ```json
  {"shuangpin": {"map": {"aa": "a", ...}}, "fuzzy": [["z", "zh"]],
   "max_candidates": 200, "dynamic_comma": false}
  ```
  双拼可直接拷内置的 `ziranma.json`（`.app/Contents/Resources/` 或仓库
  `wasm/` 下）为 `config.json`。
- `mappings.json` — 动态词映射表（见上）。
- `*.dict.txt` — 可选，追加用户词表，格式同 `data/naive_pinyin.dict.txt`
  （`拼 音 键<TAB>词:分,词:分`），后加载可提频。
- `user_freq.json` — 动态调频 + 自造词叠加层，自动维护。

改动后用输入法菜单的「重新加载词库与配置」生效。

## 排障

**安装后菜单/系统设置里看不到「手自笔录」**：macOS 26 对新注册输入法的
目录有缓存。依次试：

1. 完全退出系统设置（Cmd+Q）重开，在 键盘 → 输入法 → + → 「简体中文」分组里找。
2. `killall TextInputMenuAgent; killall SystemUIServer` 后再看。
3. 重启 Mac（最可靠的刷新方式）。
4. 仍不行且同机的鼠须管能在添加窗口看到 → 是签名差异（ad-hoc 不被
   GUI 收录）：Xcode → Settings → Accounts 登录 Apple ID（免费即可）→
   Manage Certificates → + Apple Development，得到签名身份后重签：
   `codesign --force --deep --sign "Apple Development: 名字 (TEAMID)" ~/Library/Input\ Methods/Shouzibilu.app`。

**`--select-input-source` 报 err=-50**：macOS 26 禁止程序化切换输入法
（对 Apple 自带布局同样失败），只能在系统设置 GUI 里手动添加/选中。

**只能打字母、候选窗不出现**（2026-07 实测，两个叠加的坑）：

1. IMK 的事件分发选择器是 `handleEvent:client:`（IMKServerInput 协议；
   Apple 文档里的 `handle(_:client:)` 是它的 Swift 名）。只实现
   `handle:client:` 等于没有输入入口，IMK 退回 keybinding 通道
   （`NSKeyBindingManager interpretKeyEvents:`）：字母经默认
   `insertText:` 原样进应用。与 Squirrel 二进制核对过，它实现的也是
   `handleEvent:client:`。
2. keybinding 通道下订阅 `NSEventMaskFlagsChanged` 会让
   NSKeyBindingManager 对 FlagsChanged 事件调
   `charactersIgnoringModifiers`，断言抛 `NSInternalInconsistencyException`
   并毒化会话（Ctrl+Space 切入输入法后按键全废）。实现
   `handleEvent:client:` 后事件直送控制器，无此问题。

**候选窗有框无字**：`IMKCandidates` 默认文字纯黑，在深色 HUD 面板上
隐形；且 macOS 26 上设 `NSForegroundColorAttributeName` 等属性也救不
回来 → 弃用，自绘（`CandidatePanel.mm`）。

**输入法菜单项点了没反应**：菜单 action 挂 `NSApp.delegate` 在输入法
菜单路径下不触发，要挂在 `IMKInputController` 自身（Squirrel 亦然）；
打开目录用 `selectFile:inFileViewerRootedAtPath:` 而非 `openURL:`。

**运行日志**：`log stream --predicate 'process == "Shouzibilu"' --info`

## 文件

```
macos/Info.plist               输入法 bundle 元数据（TIS 声明）
macos/mappings.json            内置默认动态词表（首启拷到用户目录）
macos/Sources/main.mm          入口：IMKServer 主循环 / 一次性管理动作
macos/Sources/AppDelegate.*    引擎单例、中英文状态、定时回写用户调频
macos/Sources/Engine.*         naive_pinyin C API 封装（配置/词表/动态词表/学习）
macos/Sources/InputController.mm  IMKInputController：按键 -> 组字 -> 候选 -> 上屏
macos/Sources/CandidatePanel.*    自绘候选窗（5 列网格、行高亮、跟随光标）
macos/Sources/Installer.*      TIS 注册/启用/选中（移植自 Squirrel）
```

无 Xcode 工程：全部经根 Makefile 以 `clang++ -fobjc-arc` 构建。
