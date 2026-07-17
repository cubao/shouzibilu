#!/usr/bin/env python3
"""build_dict.py - 雾凇词库(rime-ice) + essay.txt → naive_pinyin 紧凑文本词典

数据源：
  - cn_dicts/8105.dict.yaml   单字字表（拼音 + 25亿语料字频）
  - cn_dicts/base.dict.yaml   词库
  - cn_dicts/ext.dict.yaml    词库
  - cn_dicts/others.dict.yaml 词库
  （tencent.dict.yaml 无拼音列，靠 rime 自动注音，v1 跳过；
    41448.dict.yaml 生僻大字表，v1 跳过；
    essay.txt 无拼音列且 8105 已含字频，v1 不用，留作 v2 语言模型储备）

输出格式（每行）：
  空格分隔拼音<TAB>词:分数,词:分数,...
  分数为 0..1000 的整数（各数据源内 log10 归一化），按分数降序。

用法见 README / Makefile（make dict）。
"""

import argparse
import math
import sys
from collections import defaultdict

# 标准无声调音节表（约 410 个），用于校验词典拼音列。
# 与 naive_pinyin/src/syllable.cc 中的表保持一致。
SYLLABLES = set("""
a ai an ang ao
ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu
ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng
chi chong chou chu chua chuai chuan chuang chui chun chuo
ci cong cou cu cuan cui cun cuo
da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo
e ei en eng er
fa fan fang fei fen feng fo fou fu
ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo
ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo
ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun
ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo
la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu long lou lu lv luan lue lun luo
ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu
na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou nu nv nuan nue nuo
o ou
pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu
qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun
ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo
sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng
shi shou shu shua shuai shuan shuang shui shun shuo
si song sou su suan sui sun suo
ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan tui tun tuo
wa wai wan wang wei wen weng wo wu
xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun
ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun
za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng
zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo
zi zong zou zu zuan zui zun zuo
biang cei fiao lo lve nve
""".split())


def parse_rime_dict(path, entries, stats, require_pinyin=True):
    """解析 rime dict yaml，把 (拼音key, 词) -> 权重 合并进 entries（取最大权重）。"""
    skipped_bad_pinyin = 0
    skipped_no_pinyin = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#") or line.startswith("-"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            word = parts[0]
            if len(parts) >= 3:
                pinyin_raw, weight_raw = parts[1], parts[2]
            else:
                pinyin_raw, weight_raw = "", parts[1]
            if not pinyin_raw:
                skipped_no_pinyin += 1
                continue
            syllables = pinyin_raw.split()
            if any(s not in SYLLABLES for s in syllables):
                skipped_bad_pinyin += 1
                if stats is not None and skipped_bad_pinyin <= 5:
                    print(f"  [skip] bad pinyin: {word} | {pinyin_raw}",
                          file=sys.stderr)
                continue
            try:
                weight = float(weight_raw)
            except ValueError:
                weight = 1.0
            key = " ".join(syllables)
            pair = (key, word)
            if weight > entries.get(pair, -1):
                entries[pair] = weight
    stats["skipped_no_pinyin"] += skipped_no_pinyin
    stats["skipped_bad_pinyin"] += skipped_bad_pinyin


def normalize(weights, scale=1000):
    """{(key,word): weight} -> {(key,word): int_score}，log10 归一化到 [0, scale]。"""
    if not weights:
        return {}
    max_w = max(weights.values())
    denom = math.log10(max_w + 1.0)
    if denom <= 0:
        denom = 1.0
    return {k: int(round(scale * math.log10(w + 1.0) / denom))
            for k, w in weights.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rime-ice", required=True, help="rime-ice 仓库路径")
    ap.add_argument("--out", required=True, help="输出文件")
    ap.add_argument("--max-per-key", type=int, default=32,
                    help="每个拼音 key 最多保留多少候选（默认 32）")
    ap.add_argument("--min-score", type=int, default=1,
                    help="丢弃归一化分数低于此值的词条（默认 1/1000）")
    ap.add_argument("--min-weight", default="2:500,3:3000,4:3000,5:5000",
                    help="按音节数分档的原始权重下限，最后一档适用于更长词。"
                         "单字不受限。默认 2:500,3:3000,4:3000,5:5000")
    args = ap.parse_args()

    # 解析分档权重下限
    weight_floor = {}
    for part in args.min_weight.split(","):
        k, v = part.split(":")
        weight_floor[int(k)] = float(v)
    max_len = max(weight_floor)

    def word_kept(key, weight):
        n = key.count(" ") + 1
        floor = weight_floor.get(n, weight_floor[max_len])
        return weight >= floor

    stats = defaultdict(int)

    # 1. 单字：8105 字表（拼音 + 字频）
    char_entries = {}  # (key, char) -> weight
    parse_rime_dict(f"{args.rime_ice}/cn_dicts/8105.dict.yaml",
                    char_entries, stats)
    print(f"8105 字表: {len(char_entries)} 条", file=sys.stderr)
    char_scores = normalize(char_entries)

    # 2. 词库：base + ext + others（按分档权重下限裁剪）
    word_entries_all = {}
    for name in ("base", "ext", "others"):
        path = f"{args.rime_ice}/cn_dicts/{name}.dict.yaml"
        before = len(word_entries_all)
        parse_rime_dict(path, word_entries_all, stats)
        print(f"{name}: 累计 {len(word_entries_all)} 条"
              f" (+{len(word_entries_all) - before})", file=sys.stderr)
    word_entries = {k: w for k, w in word_entries_all.items()
                    if word_kept(k[0], w)}
    print(f"权重裁剪: {len(word_entries_all)} -> {len(word_entries)} 条",
          file=sys.stderr)
    word_scores = normalize(word_entries)

    # 3. 合并：key -> [(word, score)]
    table = defaultdict(list)
    for (key, word), score in char_scores.items():
        if score >= args.min_score:
            table[key].append((word, score))
    for (key, word), score in word_scores.items():
        if score >= args.min_score:
            table[key].append((word, score))

    # 4. 排序 + 截断 + 输出
    n_entries = 0
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# naive_pinyin dict v1\n")
        f.write("# sources: rime-ice(8105/base/ext/others)\n")
        for key in sorted(table):
            cands = sorted(table[key], key=lambda x: -x[1])
            cands = cands[:args.max_per_key]
            n_entries += len(cands)
            body = ",".join(f"{w}:{s}" for w, s in cands)
            f.write(f"{key}\t{body}\n")

    import os
    size = os.path.getsize(args.out)
    print(f"输出: {args.out}", file=sys.stderr)
    print(f"  拼音 key 数: {len(table)}", file=sys.stderr)
    print(f"  词条总数: {n_entries}", file=sys.stderr)
    print(f"  文件大小: {size / 1024 / 1024:.2f} MB", file=sys.stderr)
    print(f"  跳过(无拼音): {stats['skipped_no_pinyin']}", file=sys.stderr)
    print(f"  跳过(拼音非法): {stats['skipped_bad_pinyin']}", file=sys.stderr)


if __name__ == "__main__":
    main()
