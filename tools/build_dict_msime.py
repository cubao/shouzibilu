#!/usr/bin/env python3
"""build_dict_msime.py - msime-pinyin.db → naive_pinyin 紧凑文本词典

msime（水杉输入法）的桌面词典 msime-pinyin.db（SQLite，~74MB）是候选引擎 v2
的官方词表：引擎的词格评分公式（词典 score → ln 域映射）按它校准，本转换
保持两端逐字一致——

  score = round(1000 × ln(weight) / 18.4)     （clamp 0..1000，weight<2 丢弃）

这样引擎里 单字 (s/1000)×18.4 − 13.8 = ln(w) − ln(1e6)、多字词
20×音节 + (s/1000)×18.4 = ln(w) + 20×音节，与 msime decode.rs 的
edge_log_prob 完全相同（unigram_z=1e6、phrase_length_bonus=20）。

输入（msime-dictionary dict-v2.0.7，sha256 见 tools/msime-dict.sha256）：
  tbl_{1..7}_{声母}   1..7 音节词：key('分隔全拼)/jp(简拼,弃用)/value/weight
  tbl_others_{声母}   8+ 音节长尾（行政区划等）
  quick_parases       快短语，引擎无此概念，跳过

来源与许可：词表内容 rime-ice(GPL-3.0) 等上游合并而成（msime
resources/licenses/msime-engine-dictionary-NOTICE.md 逐项说明），随本项目
按 GPL-3.0 分发并在 README「数据来源与许可」署名。

输出格式（同旧版 build_dict.py）：空格分隔拼音<TAB>词:分数,词:分数,...
"""
import argparse
import math
import sqlite3
import sys

LN_SPAN = 18.4  # = lattice_options.h 的 unigram_char_span / unigram_phrase_span


def score_of(weight: int) -> int:
    if weight < 2:
        return 0
    return max(0, min(1000, round(1000.0 * math.log(weight) / LN_SPAN)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True, help="msime-pinyin.db 路径")
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-per-key", type=int, default=32)
    ap.add_argument("--min-score", type=int, default=1)
    args = ap.parse_args()

    conn = sqlite3.connect(f"file:{args.db}?mode=ro", uri=True)
    tables = [r[0] for r in conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name LIKE 'tbl_%' ORDER BY name")]
    if not tables:
        sys.exit("no tbl_* tables found")

    # (key, word) -> max(weight)；GROUP BY 在 DB 侧完成（tbl_1_* 有重复行）。
    table = {}  # key -> {word: score}
    total_rows = 0
    for name in tables:
        for key, word, weight in conn.execute(
                f'SELECT key, value, MAX(weight) FROM "{name}" '
                f'GROUP BY key, value'):
            total_rows += 1
            score = score_of(weight)
            if score < args.min_score:
                continue
            # 引擎词典侧用空格分隔音节。
            table.setdefault(key.replace("'", " "), {})[word] = score
    conn.close()

    n_entries = 0
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# naive_pinyin dict v2\n")
        f.write("# sources: msime-pinyin.db (dict-v2.0.7, msime-dictionary)\n")
        f.write("# score = 1000*ln(weight)/18.4, 与词格评分公式逐字一致\n")
        for key in sorted(table):
            cands = sorted(table[key].items(), key=lambda x: -x[1])
            cands = cands[: args.max_per_key]
            n_entries += len(cands)
            body = ",".join(f"{w}:{s}" for w, s in cands)
            f.write(f"{key}\t{body}\n")

    import os
    size = os.path.getsize(args.out)
    print(f"输入: {args.db} ({len(tables)} 张表, {total_rows} 组 (key,word))",
          file=sys.stderr)
    print(f"输出: {args.out}", file=sys.stderr)
    print(f"  拼音 key 数: {len(table)}", file=sys.stderr)
    print(f"  词条总数: {n_entries}", file=sys.stderr)
    print(f"  文件大小: {size / 1024 / 1024:.2f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
