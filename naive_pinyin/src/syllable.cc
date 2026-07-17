#include "syllable.h"

#include <unordered_set>

namespace naive_pinyin {

// 与 tools/build_dict.py 的 SYLLABLES 保持一致。
static const char* kSyllables[] = {
    "a",   "ai",  "an",   "ang",  "ao",
    "ba",  "bai", "ban",  "bang", "bao",  "bei", "ben", "beng", "bi",
    "bian", "biao", "bie", "bin", "bing", "bo",  "bu",
    "ca",  "cai", "can",  "cang", "cao",  "ce",  "cen", "ceng", "cha",
    "chai", "chan", "chang", "chao", "che", "chen", "cheng", "chi",
    "chong", "chou", "chu", "chua", "chuai", "chuan", "chuang", "chui",
    "chun", "chuo", "ci",  "cong", "cou", "cu",  "cuan", "cui", "cun",
    "cuo",
    "da",  "dai", "dan",  "dang", "dao",  "de",  "dei", "den", "deng",
    "di",  "dia", "dian", "diao", "die",  "ding", "diu", "dong", "dou",
    "du",  "duan", "dui", "dun",  "duo",
    "e",   "ei",  "en",   "eng",  "er",
    "fa",  "fan", "fang", "fei",  "fen",  "feng", "fo",  "fou", "fu",
    "ga",  "gai", "gan",  "gang", "gao",  "ge",  "gei", "gen", "geng",
    "gong", "gou", "gu",  "gua",  "guai", "guan", "guang", "gui", "gun",
    "guo",
    "ha",  "hai", "han",  "hang", "hao",  "he",  "hei", "hen", "heng",
    "hong", "hou", "hu",  "hua",  "huai", "huan", "huang", "hui", "hun",
    "huo",
    "ji",  "jia", "jian", "jiang", "jiao", "jie", "jin", "jing", "jiong",
    "jiu", "ju",  "juan", "jue",  "jun",
    "ka",  "kai", "kan",  "kang", "kao",  "ke",  "kei", "ken", "keng",
    "kong", "kou", "ku",  "kua",  "kuai", "kuan", "kuang", "kui", "kun",
    "kuo",
    "la",  "lai", "lan",  "lang", "lao",  "le",  "lei", "leng", "li",
    "lia", "lian", "liang", "liao", "lie", "lin", "ling", "liu", "long",
    "lou", "lu",  "lv",   "luan", "lue",  "lun", "luo", "lve",
    "ma",  "mai", "man",  "mang", "mao",  "me",  "mei", "men", "meng",
    "mi",  "mian", "miao", "mie", "min",  "ming", "miu", "mo",  "mou",
    "mu",
    "na",  "nai", "nan",  "nang", "nao",  "ne",  "nei", "nen", "neng",
    "ni",  "nian", "niang", "niao", "nie", "nin", "ning", "niu", "nong",
    "nou", "nu",  "nv",   "nuan", "nue",  "nuo", "nve",
    "o",   "ou",
    "pa",  "pai", "pan",  "pang", "pao",  "pei", "pen", "peng", "pi",
    "pian", "piao", "pie", "pin", "ping", "po",  "pou", "pu",
    "qi",  "qia", "qian", "qiang", "qiao", "qie", "qin", "qing", "qiong",
    "qiu", "qu",  "quan", "que",  "qun",
    "ran", "rang", "rao", "re",   "ren",  "reng", "ri",  "rong", "rou",
    "ru",  "rua", "ruan", "rui",  "run",  "ruo",
    "sa",  "sai", "san",  "sang", "sao",  "se",  "sen", "seng", "sha",
    "shai", "shan", "shang", "shao", "she", "shei", "shen", "sheng",
    "shi", "shou", "shu", "shua", "shuai", "shuan", "shuang", "shui",
    "shun", "shuo", "si",  "song", "sou", "su",  "suan", "sui", "sun",
    "suo",
    "ta",  "tai", "tan",  "tang", "tao",  "te",  "tei", "teng", "ti",
    "tian", "tiao", "tie", "ting", "tong", "tou", "tu",  "tuan", "tui",
    "tun", "tuo",
    "wa",  "wai", "wan",  "wang", "wei",  "wen", "weng", "wo",  "wu",
    "xi",  "xia", "xian", "xiang", "xiao", "xie", "xin", "xing", "xiong",
    "xiu", "xu",  "xuan", "xue",  "xun",
    "ya",  "yan", "yang", "yao",  "ye",   "yi",  "yin", "ying", "yo",
    "yong", "you", "yu",  "yuan", "yue",  "yun",
    "za",  "zai", "zan",  "zang", "zao",  "ze",  "zei", "zen", "zeng",
    "zha", "zhai", "zhan", "zhang", "zhao", "zhe", "zhei", "zhen",
    "zheng", "zhi", "zhong", "zhou", "zhu", "zhua", "zhuai", "zhuan",
    "zhuang", "zhui", "zhun", "zhuo", "zi", "zong", "zou", "zu",
    "zuan", "zui", "zun",  "zuo",
    // 口语音节
    "biang", "cei", "fiao", "lo",
};

static const std::unordered_set<std::string>& SyllableSet() {
  static const std::unordered_set<std::string> s(
      std::begin(kSyllables), std::end(kSyllables));
  return s;
}

bool IsSyllable(const std::string& s) { return SyllableSet().count(s) > 0; }

void SplitSyllable(const std::string& syl, std::string* initial,
                   std::string* final_) {
  // 声母表：先匹配双字母声母，再单字母。
  static const char* kInitials[] = {
      "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
      "g",  "k",  "h",  "j", "q", "x", "r", "z", "c", "s", "y", "w",
  };
  for (const char* ini : kInitials) {
    size_t len = std::string(ini).size();
    if (syl.compare(0, len, ini) == 0) {
      // 排除误配：如 "shi" 的 "sh" 正确；但 "s" + "hi" 不合法，
      // 由于输入保证是合法音节，双字母优先即可。
      *initial = ini;
      *final_ = syl.substr(len);
      return;
    }
  }
  initial->clear();
  *final_ = syl;
}

Segmentation SegmentFullPinyin(const std::string& input) {
  Segmentation seg;
  const int n = static_cast<int>(input.size());
  seg.length = n;
  seg.edges_from.resize(n + 1);
  seg.boundary.assign(n + 1, false);

  static constexpr int kMaxSyllableLen = 6;  // zhuang/shuang/chuang

  int seg_start = 0;
  for (int i = 0; i <= n; ++i) {
    if (i == n || input[i] == '\'') {
      // [seg_start, i) 是一个 apostrophe 分隔段。
      if (i < n) seg.boundary[i] = true;
      for (int s = seg_start; s < i; ++s) {
        for (int len = 1; len <= kMaxSyllableLen && s + len <= i; ++len) {
          std::string sub = input.substr(s, len);
          if (IsSyllable(sub)) {
            seg.edges_from[s].push_back({s, s + len, std::move(sub)});
          }
        }
      }
      seg_start = i + 1;
    }
  }
  return seg;
}

Segmentation SegmentShuangpin(
    const std::string& input,
    const std::unordered_map<std::string, std::string>& code_map) {
  Segmentation seg;
  const int n = static_cast<int>(input.size());
  seg.length = n;
  seg.edges_from.resize(n + 1);
  seg.boundary.assign(n + 1, false);

  int seg_start = 0;
  for (int i = 0; i <= n; ++i) {
    if (i == n || input[i] == '\'') {
      if (i < n) seg.boundary[i] = true;
      for (int s = seg_start; s + 2 <= i; s += 2) {
        auto it = code_map.find(input.substr(s, 2));
        if (it != code_map.end()) {
          seg.edges_from[s].push_back({s, s + 2, it->second});
        }
      }
      seg_start = i + 1;
    }
  }
  return seg;
}

}  // namespace naive_pinyin
