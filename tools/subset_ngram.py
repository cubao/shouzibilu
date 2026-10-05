#!/usr/bin/env python3
"""subset_ngram.py - 从全量 MSNG n-gram 表裁出紧凑档

按 |增量值| 降序保留前 K 条，输出仍是合法的 MSNG v1 表（见
naive_pinyin/src/ngram.h）。缺失条目在解码端等价于增量 0（不奖不罚），
因此丢掉 |v| 小的长尾对打分几乎无损，换来 wasm/移动端更小的载荷。

来源与署名：全量表为 msime（metasequoiaime/msime）以中文维基百科 dump
（CC-BY-SA 4.0）统计的发布产物，本子集再分发时须保留对中文维基百科的
署名并按 CC-BY-SA 4.0 提供（见 README「数据来源与许可」）。

用法:
  python3 tools/subset_ngram.py --input data/msime-bigram.bin \
      --output data/naive_pinyin.bigram.bin --keep 300000
"""

import argparse
import struct
import sys

HEADER = 16
ENTRY = 12


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--keep", type=int, default=None,
                    help="额外条数上限（按 |值| 降序裁，默认不设）")
    ap.add_argument("--min-abs", type=float, default=1.0,
                    help="保留 |值| >= 此阈值的条目（默认 1.0）；"
                         "0 等价于保留全表")
    args = ap.parse_args()

    data = open(args.input, "rb").read()
    if len(data) < HEADER or data[:4] != b"MSNG":
        sys.exit(f"not an MSNG table: {args.input}")
    version, count, _ = struct.unpack_from("<III", data, 4)
    if version != 1:
        sys.exit(f"unsupported MSNG version {version}")
    need = HEADER + count * ENTRY
    if need > len(data):
        sys.exit("truncated table")
    keys = struct.unpack_from(f"<{count}Q", data, HEADER)
    values = struct.unpack_from(f"<{count}f", data, HEADER + count * 8)

    order = sorted(range(count),
                   key=lambda i: (-abs(values[i]), keys[i]))
    keep = [i for i in order if abs(values[i]) >= args.min_abs]
    if args.keep is not None:
        keep = keep[: args.keep]
    keep_sorted = sorted(keep, key=lambda i: keys[i])
    # 稳定排序下重复键可能都入选：按 (键, 值) 去重，取第一条（与查找语义一致）。
    seen = set()
    entries = []
    for i in keep_sorted:
        if keys[i] in seen:
            continue
        seen.add(keys[i])
        entries.append((keys[i], values[i]))

    out = bytearray()
    out += b"MSNG"
    out += struct.pack("<III", 1, len(entries), 0)
    for k, _ in entries:
        out += struct.pack("<Q", k)
    for _, v in entries:
        out += struct.pack("<f", v)
    open(args.output, "wb").write(out)

    abs_max = max(abs(v) for _, v in entries) if entries else 0.0
    abs_min = min(abs(v) for _, v in entries) if entries else 0.0
    print(f"输入: {args.input} ({count} 条, {len(data)/1048576:.1f} MB)")
    print(f"输出: {args.output} ({len(entries)} 条, {len(out)/1048576:.1f} MB)")
    print(f"保留档 |v| 范围: [{abs_min:.3f}, {abs_max:.3f}] (全表值域 ±3)")


if __name__ == "__main__":
    main()
