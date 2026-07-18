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

cli: $(CLI)

$(CLI): $(LIB_OBJS) tools/cli.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

# ---- wasm ----
# 需要 em++ 在 PATH 中：source ../emsdk/emsdk_env.sh
wasm: $(WASM_OUT)

$(WASM_OUT): $(LIB_SRCS) $(NP_DIR)/src/*.h $(NP_DIR)/include/naive_pinyin/*.h Makefile
	$(EMXX) $(EMXXFLAGS) $(INCLUDES) $(LIB_SRCS) -o $@ \
	  -s EXPORTED_FUNCTIONS='["_np_create","_np_load_dict","_np_query","_np_destroy","_malloc","_free"]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","lengthBytesUTF8","stringToUTF8","HEAPU8"]' \
	  -s MODULARIZE=1 \
	  -s EXPORT_NAME=createNaivePinyin \
	  -s ALLOW_MEMORY_GROWTH=1 \
	  -s ENVIRONMENT=web,node

smoke: wasm
	node wasm/smoke_test.js

# 浏览器 demo（从仓库根目录起服务，demo 引用 ../wasm ../data；no-store 禁缓存）
demo: $(WASM_OUT) $(DICT_OUT) $(WASM_DIR)/ziranma.json
	python3 tools/serve.py 8000

# ---- 词典 ----
dict: $(DICT_OUT)

$(DICT_OUT): tools/build_dict.py
	python3 tools/build_dict.py \
	  --rime-ice $(RIME_ICE) \
	  --out $@

# 自然码双拼默认配置（前端作为默认 shuangpin map）
ziranma: $(WASM_DIR)/ziranma.json

$(WASM_DIR)/ziranma.json: tools/gen_ziranma.py tools/build_dict.py
	python3 tools/gen_ziranma.py > $@

clean:
	rm -rf $(BUILD) $(WASM_OUT) $(WASM_DIR)/naive_pinyin.wasm
