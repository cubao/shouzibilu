#!/usr/bin/env python3
"""regression.py - 排序质量回归测试集

通过 native CLI 批量查询并断言候选排序，用于：
  - 词库 blend 前后的质量对比（无回退）
  - 打分参数(segment_penalty 等)调整时的回归

用法:
  python3 tools/regression.py [--cli build/native/cli]
                              [--dict data/naive_pinyin.dict.txt]
                              [--config '{}'] [-v]
"""

import argparse
import json
import subprocess
import sys

# 断言类型:
#   ("first", "X")      首选必须是 X
#   ("above", ("A","B")) A 与 B 都在候选中且 A 排在 B 前
#   ("has", "X")        候选中包含 X
CASES = [
    # (输入, [断言...], 备注)
    ("nihaoshijie", [("first", "你好世界")], "整句基本"),
    ("zhonghuarenmingongheguo", [("first", "中华人民共和国")], "长词"),
    ("shijie", [("first", "世界"), ("above", ("世界", "使节"))], "高频二字"),
    ("jinan", [("above", ("济南", "极难"))], "jieba blend 修复目标"),
    ("xian", [("first", "先")], "单字优先于生僻词"),
    ("woxihuan", [("first", "我喜欢")], "常用三字"),
    ("beijing", [("first", "北京")], "地名"),
    ("zhongguo", [("first", "中国")], "国名"),
    ("jisuanji", [("first", "计算机")], "三字术语"),
    ("suanfa", [("first", "算法")], "术语"),
    ("tianqi", [("first", "天气")], "常用二字"),
    ("pengyou", [("first", "朋友")], "常用二字"),
    ("gongzuo", [("first", "工作")], "常用二字"),
    ("xuexi", [("first", "学习")], "常用二字"),
    ("chengxu", [("first", "程序")], "常用二字"),
    ("shijian", [("first", "时间")], "多音字常见义"),
    ("yinwei", [("first", "因为")], "常用连词"),
    ("meiyou", [("first", "没有")], "高频词"),
    ("women", [("first", "我们")], "高频词"),
    ("xianzai", [("first", "现在")], "高频词"),
]

# 语言模型（--lm）开启下的整句质量断言：词格 bigram/trigram 解码。
LM_CASES = [
    # (输入, [断言...], 备注)
    ("nihaoshijie", [("first", "你好世界")], "整句基本"),
    ("jintiandetianqihenhao", [("first", "今天的天气很好")], "天气句"),
    ("womenxuyaojiejuewenti", [("first", "我们需要解决问题")], "需要解决"),
    ("tianxiawuxing", [("has", "天下五行"), ("has", "天下无形"),
                       ("above", ("天下五行", "提案下午行"))],
     "歧义切分 ti+an"),
    ("jiejuefangan", [("first", "解决方案"),
                      ("above", ("解决方案", "解决反感"))], "精确行全键优先"),
    ("womenyaoyanjiuzhegefangan",
     [("has", "我们要研究这个方案")], "备选切分递补方案"),
    ("zheshiyigehenhaodejiejuefangan",
     [("has", "这是一个很好的解决方案")], "长句备选递补"),
    ("jintiankaihui", [("first", "今天开会")], "开会句"),
    ("tamenlaileda", [("first", "他们来了大")], "尾字单字"),
    ("zhongguorenminjiefangjun", [("first", "中国人民解放军")], "专名整词"),
]


def run_cases(cli, dict_path, config, cases, verbose, lm_flags=None):
    inputs = [c[0] for c in cases]
    cmd = [cli] + (lm_flags or []) + [dict_path, config] + inputs
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    results = {}
    for line in proc.stdout.splitlines():
        inp, _, payload = line.partition(" => ")
        results[inp] = json.loads(payload)

    passed = failed = 0
    for inp, assertions, note in cases:
        cands = results[inp]["candidates"]
        texts = [c["text"] for c in cands]
        for kind, arg in assertions:
            if kind == "first":
                ok = texts and texts[0] == arg
                desc = f"first == {arg}"
            elif kind == "above":
                a, b = arg
                ok = a in texts and b in texts and texts.index(a) < texts.index(b)
                desc = f"{a} above {b}"
            elif kind == "has":
                ok = arg in texts
                desc = f"has {arg}"
            else:
                raise ValueError(kind)
            status = "PASS" if ok else "FAIL"
            if ok:
                passed += 1
            else:
                failed += 1
            if verbose or not ok:
                top = " ".join(texts[:5])
                print(f"{status}  {inp:28s} {desc:24s} [{note}] top5: {top}")
    return passed, failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", default="build/native/cli")
    ap.add_argument("--dict", default="data/naive_pinyin.dict.txt")
    ap.add_argument("--config", default="{}")
    ap.add_argument("--lm", default=None,
                    help="bigram 表路径；提供时额外运行 LM_CASES 断言")
    ap.add_argument("--trigram", default=None, help="trigram 表路径（可选）")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    lm_flags = []
    if args.lm:
        lm_flags += ["--lm", args.lm]
    if args.trigram:
        lm_flags += ["--trigram", args.trigram]

    passed, failed = run_cases(args.cli, args.dict, args.config,
                               CASES, args.verbose)
    if args.lm:
        lm_passed, lm_failed = run_cases(args.cli, args.dict, args.config,
                                         LM_CASES, args.verbose, lm_flags)
        passed += lm_passed
        failed += lm_failed
    print("----")
    print(f"{'OK' if failed == 0 else 'FAILED'}: {passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
