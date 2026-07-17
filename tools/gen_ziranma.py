#!/usr/bin/env python3
"""gen_ziranma.py - 生成自然码双拼的「2字母码 -> 音节」完整映射表 (JSON)

映射规则提取自 rime-ice double_pinyin.schema.yaml 的 speller/algebra
（与 rime-double-pinyin 一致）。

输出: {"shuangpin": {"map": {"ni": "ni", "hk": "hao", ...}}}
用法:
  python3 tools/gen_ziranma.py            # 打印 JSON 到 stdout
  python3 tools/gen_ziranma.py --check    # 额外打印校验信息到 stderr
"""

import argparse
import json
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from build_dict import SYLLABLES  # noqa: E402

# 韵母 -> 键位（单字母韵母映射到自身，未列出）。
FINAL_KEY = {
    "iu": "q", "ia": "w", "ua": "w",
    "uan": "r", "van": "r",
    "ue": "t", "ve": "t",
    "ing": "y", "uai": "y",
    "uo": "o",
    "un": "p", "vn": "p",
    "iong": "s", "ong": "s",
    "iang": "d", "uang": "d",
    "en": "f", "eng": "g", "ang": "h",
    "ian": "m", "an": "j",
    "iao": "c", "ao": "k", "ai": "l",
    "ei": "z", "ie": "x", "ui": "v",
    "ou": "b", "in": "n",
}

# 双字母声母 -> 键位；单字母声母即自身。
INITIAL_KEY = {"zh": "v", "ch": "i", "sh": "u"}

INITIALS = ("zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
            "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w")


def split_syllable(syl):
    """拆成 (声母, 韵母)，与 C++ SplitSyllable 一致。"""
    for ini in INITIALS:
        if syl.startswith(ini):
            return ini, syl[len(ini):]
    return "", syl


def zero_initial_codes(syl):
    """零声母音节(a/o/e 开头)的编码。

    规则(来自 algebra):
      xform/^([aoe])(ng)?$/$1$1$2/   -> 单字母 doubling: a->aa; ang->aang->ah
      derive/^([aoe])([ioun])$/$1$1$2/ -> 双字母韵母同时保留原形与 doubling 派生
    """
    assert syl[0] in "aoe", syl
    if len(syl) == 1:
        return [syl * 2]                      # a->aa, e->ee, o->oo
    if syl.endswith("ng"):                    # ang->ah, eng->eg
        return [syl[0] + FINAL_KEY[syl]]
    # 双字母韵母: 原形 + doubling 派生 (an->an/aj, ai->ai/al, ...)
    # derive 出 aai 式后按 (.)xx$ 规则取韵尾键位
    codes = [syl]
    if syl in FINAL_KEY:
        codes.append(syl[0] + FINAL_KEY[syl])
    return codes


def syllable_codes(syl):
    """返回该音节的全部合法 2 字母码。"""
    initial, final = split_syllable(syl)
    if not initial:
        return zero_initial_codes(syl)
    ikey = INITIAL_KEY.get(initial, initial)
    fkey = FINAL_KEY.get(final, final)
    assert len(fkey) == 1, f"no key for final {final} ({syl})"
    codes = [ikey + fkey]
    # derive/^([jqxy])u$/$1v/:  qu/ju/xu/yu 也可打 v
    if initial in ("j", "q", "x", "y") and final == "u":
        codes.append(ikey + "v")
    return codes


# 码位冲突的人工裁决（自然码设计上的固有冲突）：
#   lo: 咯(lo) vs 罗(luo)   -> 取 luo（咯走 ge/ka 读音）
#   lt: lue vs lve          -> 取 lve（rime-ice 词库用 v 记法）
#   nt: nue vs nve          -> 取 nve
COLLISION_PICK = {"lo": "luo", "lt": "lve", "nt": "nve"}


def build_map():
    code2syl = {}
    for syl in sorted(SYLLABLES):
        for code in syllable_codes(syl):
            if code in code2syl and code2syl[code] != syl:
                pick = COLLISION_PICK.get(code)
                assert pick, f"未裁决的码位冲突: {code} -> {code2syl[code]} / {syl}"
                code2syl[code] = pick
            else:
                code2syl[code] = syl
    return code2syl


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="打印校验信息")
    args = ap.parse_args()

    code2syl = build_map()
    config = {"shuangpin": {"map": dict(sorted(code2syl.items()))}}
    print(json.dumps(config, ensure_ascii=False, indent=2))

    if args.check:
        print(f"音节数: {len(SYLLABLES)}", file=sys.stderr)
        print(f"码位数: {len(code2syl)}", file=sys.stderr)
        for code in ("ni", "hk", "ud", "vh", "aa", "an", "aj", "ah",
                     "yv", "yu", "lt", "nv"):
            print(f"  {code} -> {code2syl.get(code)}", file=sys.stderr)


if __name__ == "__main__":
    main()
