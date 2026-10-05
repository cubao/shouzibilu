#!/usr/bin/env python3
"""stamp_build.py - 把构建版本（日期 + commit 短哈希）写进 index.html 的
BUILD_INFO 常量，页面上明示版本，避免缓存新旧混用看不出来。

顺带把 data/ 下页面会加载的文件原始字节数写进 DATA_SIZES：gzip 传输时
Content-Length 是压缩字节数，而浏览器 fetch 读到的是解压后字节，进度条
分母需要原始大小。

用法: python3 tools/stamp_build.py index.html   （make wasm 自动调用）
幂等：内容不变则不写。"""
import os
import re
import subprocess
import sys

# index.html fetchWithProgress 会取分母的数据文件
DATA_FILES = ("naive_pinyin.dict.txt", "naive_pinyin.bigram.bin")


def data_sizes(path):
    data_dir = os.path.join(os.path.dirname(os.path.abspath(path)), "data")
    sizes = {}
    for name in DATA_FILES:
        p = os.path.join(data_dir, name)
        if os.path.isfile(p):
            sizes[f"data/{name}"] = os.path.getsize(p)
    return sizes


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "index.html"
    s = open(path, encoding="utf-8").read()
    orig_s = s
    try:
        info = subprocess.run(
            ["git", "log", "-1", "--format=%cs %h"],
            capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        info = ""
    if not info:
        print("stamp_build: not a git worktree, skip", file=sys.stderr)
        return
    s = re.sub(r'(const BUILD_INFO = ")[^"]*(")', rf"\g<1>{info}\g<2>", s)
    sizes = data_sizes(path)
    if sizes:
        entries = ", ".join(f'"{k}": {v}' for k, v in sorted(sizes.items()))
        s = re.sub(r"(const DATA_SIZES = \{)[^}]*(\})", rf"\g<1> {entries} \g<2>", s)
    if s != orig_s:
        open(path, "w", encoding="utf-8").write(s)
        print(f"stamp_build: BUILD_INFO -> {info}; DATA_SIZES -> {sizes}")
    else:
        print(f"stamp_build: already up to date ({info}, {sizes})")


if __name__ == "__main__":
    main()
