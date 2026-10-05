#!/usr/bin/env python3
"""stamp_build.py - 把构建版本（日期 + commit 短哈希）写进 index.html 的
BUILD_INFO 常量，页面上明示版本，避免缓存新旧混用看不出来。

用法: python3 tools/stamp_build.py index.html   （make wasm 自动调用）
幂等：内容不变则不写。"""
import re
import subprocess
import sys


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "index.html"
    try:
        info = subprocess.run(
            ["git", "log", "-1", "--format=%cs %h"],
            capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        info = ""
    if not info:
        print("stamp_build: not a git worktree, skip", file=sys.stderr)
        return
    s = open(path, encoding="utf-8").read()
    new = re.sub(r'(const BUILD_INFO = ")[^"]*(")', rf"\g<1>{info}\g<2>", s)
    if new != s:
        open(path, "w", encoding="utf-8").write(new)
        print(f"stamp_build: BUILD_INFO -> {info}")
    else:
        print(f"stamp_build: BUILD_INFO already {info}")


if __name__ == "__main__":
    main()
