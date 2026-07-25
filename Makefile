# 手自笔录 (shouzibilu) - naive_pinyin
#
# 常用命令：
#   make test    构建并运行 native 单元测试（日常开发主用）
#   make native  只构建 native 静态库 + 测试
#   make wasm    构建 WebAssembly 产物（需要 emsdk，见 README）
#   make dict    生成精简词典（需要 ../rime-ice 与 ../rime-essay）
#   make smoke   node 冒烟测试 wasm 产物
#   make clean

CXX       ?= clang++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
EMXX      ?= em++
EMXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

NP_DIR       := naive_pinyin
THIRD_PARTY  := third_party
INCLUDES     := -I$(NP_DIR)/include -I$(THIRD_PARTY)

BUILD        := build
NATIVE_DIR   := $(BUILD)/native
WASM_DIR     := wasm

LIB_SRCS  := $(wildcard $(NP_DIR)/src/*.cc)
TEST_SRCS := $(wildcard tests/*.cc)

LIB_OBJS  := $(patsubst $(NP_DIR)/src/%.cc,$(NATIVE_DIR)/%.o,$(LIB_SRCS))
TEST_OBJS := $(patsubst tests/%.cc,$(NATIVE_DIR)/test_%.o,$(TEST_SRCS))

RUN_TESTS := $(NATIVE_DIR)/run_tests
CLI       := $(NATIVE_DIR)/cli

WASM_OUT := $(WASM_DIR)/naive_pinyin.js

# 词典工具
DICT_OUT   := data/naive_pinyin.dict.txt
RIME_ICE   := ../rime-ice
JIEBA_DICT := data/jieba_dict.txt

.PHONY: all native test wasm smoke dict clean

all: native

native: $(RUN_TESTS)

$(NATIVE_DIR):
	mkdir -p $@

$(NATIVE_DIR)/%.o: $(NP_DIR)/src/%.cc | $(NATIVE_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(NATIVE_DIR)/test_%.o: tests/%.cc | $(NATIVE_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Itests -MMD -MP -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

$(RUN_TESTS): $(LIB_OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(RUN_TESTS)
	./$(RUN_TESTS)

# 排序质量回归（依赖词典与 cli）
regression: $(CLI) $(DICT_OUT)
	python3 tools/regression.py --cli $(CLI) --dict $(DICT_OUT) -v

cli: $(CLI)

$(CLI): $(LIB_OBJS) tools/cli.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

# ---- wasm ----
# 需要 em++ 在 PATH 中：source ../emsdk/emsdk_env.sh
wasm: $(WASM_OUT)

$(WASM_OUT): $(LIB_SRCS) $(NP_DIR)/src/*.h $(NP_DIR)/include/naive_pinyin/*.h Makefile
	$(EMXX) $(EMXXFLAGS) $(INCLUDES) $(LIB_SRCS) -o $@ \
	  -s EXPORTED_FUNCTIONS='["_np_create","_np_load_dict","_np_query","_np_segment","_np_commit","_np_learn_word","_np_dump_user","_np_destroy","_malloc","_free"]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","lengthBytesUTF8","stringToUTF8","HEAPU8"]' \
	  -s MODULARIZE=1 \
	  -s EXPORT_NAME=createNaivePinyin \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -s ENVIRONMENT=web,node

smoke: wasm
	node wasm/smoke_test.js

# 浏览器端到端冒烟（需 playwright 与本机 Chromium; 缺依赖自动 SKIP）
e2e:
	node tests/e2e_browser.js

# ---- npm 包 @cubao/naive-pinyin ----
# 组装 npm/ 目录（产物拷入, 静态文件已在库中）, 发布见 README
NPM_DIR := npm

npm: $(WASM_OUT) $(DICT_OUT) $(WASM_DIR)/ziranma.json
	cp $(WASM_OUT) $(WASM_DIR)/naive_pinyin.wasm $(NPM_DIR)/
	cp $(DICT_OUT) $(NPM_DIR)/naive_pinyin.dict.txt
	cp $(WASM_DIR)/ziranma.json $(NPM_DIR)/
	cp ime-editor.js virtual-keyboard.js LICENSE $(NPM_DIR)/
	@echo "npm 包已组装: $(NPM_DIR)/ (发布: make npm-publish)"

npm-test: npm
	node $(NPM_DIR)/test.js

# 发布到官方 registry（全局默认是 npmmirror 只读镜像, 禁止发布, 必须显式覆盖;
# scoped 包默认私有, 需 --access public; ~/.npmrc 的 granular token 已绕过 2FA, 无需 OTP）
NPM_REGISTRY := https://registry.npmjs.org

npm-publish: npm
	cd $(NPM_DIR) && npm pack --dry-run --registry=$(NPM_REGISTRY)
	cd $(NPM_DIR) && npm publish --access public --registry=$(NPM_REGISTRY)

# 浏览器 demo（index.html 在仓库根, 与 GitHub Pages 同构; no-store 禁缓存)
demo: $(WASM_OUT) $(DICT_OUT) $(WASM_DIR)/ziranma.json
	python3 tools/serve.py 8000

# ---- macOS 输入法（参考 ../squirrel，复用同一 naive_pinyin 核心）----
# 常用命令：
#   make macos          构建 build/macos/Shouzibilu.app
#   make macos-install  装入 ~/Library/Input Methods 并注册/启用（之后手动选中）
MACOS_BUILD := $(BUILD)/macos
APP_NAME    := Shouzibilu
APP         := $(MACOS_BUILD)/$(APP_NAME).app
MACOS_SRCS  := $(wildcard macos/Sources/*.mm)
MACOS_OBJS  := $(patsubst macos/Sources/%.mm,$(MACOS_BUILD)/%.o,$(MACOS_SRCS))
MACOS_BIN   := $(APP)/Contents/MacOS/$(APP_NAME)
IM_INSTALL  := $(HOME)/Library/Input Methods

.PHONY: macos macos-install

macos: $(APP)
	@echo "已构建 $(APP)"

$(MACOS_BUILD)/%.o: macos/Sources/%.mm | $(NATIVE_DIR)
	mkdir -p $(MACOS_BUILD)
	$(CXX) $(CXXFLAGS) -fobjc-arc $(INCLUDES) -Imacos/Sources -MMD -MP -c $< -o $@

-include $(MACOS_OBJS:.o=.d)

$(MACOS_BIN): $(MACOS_OBJS) $(LIB_OBJS)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	$(CXX) $(CXXFLAGS) -fobjc-arc $^ -o $@ \
	  -framework Cocoa -framework InputMethodKit -framework Carbon
	cp macos/Info.plist $(APP)/Contents/Info.plist
	cp $(DICT_OUT) $(WASM_DIR)/ziranma.json $(APP)/Contents/Resources/

$(APP): $(MACOS_BIN)
	codesign --force --deep --sign - $(APP)

# 安装到用户输入法目录并注册。装完后在系统设置或菜单栏选中"手自笔录"。
macos-install: macos
	mkdir -p "$(IM_INSTALL)"
	-"$(IM_INSTALL)/$(APP_NAME).app/Contents/MacOS/$(APP_NAME)" --quit
	rsync -a --delete "$(APP)/" "$(IM_INSTALL)/$(APP_NAME).app/"
	"$(IM_INSTALL)/$(APP_NAME).app/Contents/MacOS/$(APP_NAME)" --install
	"$(IM_INSTALL)/$(APP_NAME).app/Contents/MacOS/$(APP_NAME)" --enable-input-source
	@echo "已安装到 $(IM_INSTALL)/$(APP_NAME).app"
	@echo "下一步: 系统设置 > 键盘 > 输入法 中添加/选中「手自笔录」，或运行:"
	@echo "  '$(IM_INSTALL)/$(APP_NAME).app/Contents/MacOS/$(APP_NAME)' --select-input-source"

# ---- 词典 ----
dict: $(DICT_OUT)

$(JIEBA_DICT):
	curl -sL -o $@ https://raw.githubusercontent.com/fxsjy/jieba/master/jieba/dict.txt

$(DICT_OUT): tools/build_dict.py $(JIEBA_DICT)
	python3 tools/build_dict.py \
	  --rime-ice $(RIME_ICE) \
	  --jieba $(JIEBA_DICT) \
	  --out $@

# 自然码双拼默认配置（前端作为默认 shuangpin map）
ziranma: $(WASM_DIR)/ziranma.json

$(WASM_DIR)/ziranma.json: tools/gen_ziranma.py tools/build_dict.py
	python3 tools/gen_ziranma.py > $@

clean:
	rm -rf $(BUILD)

# 连 wasm 产物一起清(产物已入库供 GitHub Pages 部署, 日常 clean 不动)
distclean: clean
	rm -f $(WASM_OUT) $(WASM_DIR)/naive_pinyin.wasm
