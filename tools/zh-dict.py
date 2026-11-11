#!/usr/bin/env python3
"""Build the Chinese add-on's Hangul reading data (languages/zh/dict/): zh.map and zh.txt.

    python tools/zh-dict.py luna_pinyin.dict.yaml essay.txt TSCharacters.txt TSPhrases.txt [OUT_DIR] [--exclude FILE]

zh.map: Hangul (one or two syllables) -> pinyin syllables (tab, then readings separated by spaces). Chinese is
  typed in Hangul as Koreans write Mandarin (중궈 = zhongguo, 베이징 = beijing, also short forms like 베징); the
  dictionary decides which readings exist.
zh.txt: pinyin without spaces or tones -> word (Simplified), `reading<TAB>word<TAB><TAB>weight`, sorted by the
  reading's UTF-8 bytes, most frequent first within a reading.
Sources: rime luna_pinyin.dict.yaml (character and word readings) and essay.txt (word frequencies), LGPL-3.0,
  https://github.com/rime; OpenCC TSCharacters.txt / TSPhrases.txt (Traditional -> Simplified), Apache-2.0,
  https://github.com/BYVoid/OpenCC.
"""
import sys

PINNED = ("rime/rime-luna-pinyin 56b934b099dfbeab842320f13aa8b461a6ab3e42, rime/rime-essay "
          "054920de4f54c9e5994276a96a4fc2a35cb51aa3, BYVoid/OpenCC (TSCharacters, TSPhrases)")
MIN_WEIGHT = 30          # essay words below this frequency are left out (keeps the file small)

CHO = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
JUNG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
JONG = ["", "ㄱ", "ㄲ", "ㄳ", "ㄴ", "ㄵ", "ㄶ", "ㄷ", "ㄹ", "ㄺ", "ㄻ", "ㄼ", "ㄽ", "ㄾ", "ㄿ", "ㅀ", "ㅁ", "ㅂ", "ㅄ",
        "ㅅ", "ㅆ", "ㅇ", "ㅈ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"]


def syl(c, v, j=""):
    return chr(0xAC00 + (CHO.index(c) * 21 + JUNG.index(v)) * 28 + JONG.index(j))


# Korean spelling of Mandarin (외래어 표기법) plus common variants
INITIALS = {
    "b": "ㅂ", "p": "ㅍ", "m": "ㅁ", "f": "ㅍㅎ", "d": "ㄷ", "t": "ㅌ", "n": "ㄴ", "l": "ㄹ", "g": "ㄱ", "k": "ㅋ",
    "h": "ㅎ", "j": "ㅈ", "q": "ㅊ", "x": "ㅅ", "zh": "ㅈ", "ch": "ㅊ", "sh": "ㅅ", "r": "ㄹ", "z": "ㅉㅈ",
    "c": "ㅊㅉ", "s": "ㅆㅅ", "": "ㅇ", "y": "ㅇ", "w": "ㅇ",
}
# final -> list of spellings; a spelling is a list of (vowel, final consonant) syllables, the first one taking the
# initial, the others starting with ㅇ
F = lambda *s: [tuple(x.split("+")) if "+" in x else (x, "") for x in s]
FINALS = {
    "a": [F("ㅏ")], "o": [F("ㅗ"), F("ㅝ")], "e": [F("ㅓ"), F("ㅡ")], "ai": [F("ㅏ", "ㅣ"), F("ㅐ")],
    "ei": [F("ㅔ", "ㅣ"), F("ㅔ")], "ao": [F("ㅏ", "ㅗ"), F("ㅏ", "ㅜ")], "ou": [F("ㅓ", "ㅜ"), F("ㅗ", "ㅜ"), F("ㅗ")],
    "an": [F("ㅏ+ㄴ")], "en": [F("ㅓ+ㄴ"), F("ㅡ+ㄴ")], "ang": [F("ㅏ+ㅇ")], "eng": [F("ㅓ+ㅇ"), F("ㅡ+ㅇ")],
    "ong": [F("ㅜ+ㅇ"), F("ㅗ+ㅇ")], "er": [F("ㅓ+ㄹ")],
    "i": [F("ㅣ")], "ia": [F("ㅑ"), F("ㅏ")], "iao": [F("ㅑ", "ㅗ"), F("ㅏ", "ㅗ"), F("ㅛ")],
    "ie": [F("ㅖ"), F("ㅔ")], "iu": [F("ㅠ"), F("ㅜ"), F("ㅣ", "ㅜ")],
    "ian": [F("ㅖ+ㄴ"), F("ㅔ+ㄴ"), F("ㅕ+ㄴ"), F("ㅣ", "ㅔ+ㄴ")], "in": [F("ㅣ+ㄴ")], "iang": [F("ㅑ+ㅇ"), F("ㅏ+ㅇ")],
    "ing": [F("ㅣ+ㅇ")], "iong": [F("ㅠ+ㅇ"), F("ㅜ+ㅇ")],
    "u": [F("ㅜ")], "ua": [F("ㅘ")], "uo": [F("ㅝ"), F("ㅜ", "ㅗ")], "uai": [F("ㅘ", "ㅣ"), F("ㅙ")],
    "ui": [F("ㅜ", "ㅣ"), F("ㅞ", "ㅣ"), F("ㅟ"), F("ㅞ")], "uan": [F("ㅘ+ㄴ")], "un": [F("ㅜ+ㄴ")],
    "uang": [F("ㅘ+ㅇ")],
    "v": [F("ㅟ")], "ve": [F("ㅞ"), F("ㅟ", "ㅔ")], "van": [F("ㅟ", "ㅏ+ㄴ"), F("ㅝ+ㄴ")], "vn": [F("ㅟ+ㄴ")],
}
SPECIAL = {  # zero-initial syllables whose spelling differs from initial + final
    "wei": [F("ㅞ", "ㅣ"), F("ㅞ")], "wen": [F("ㅝ+ㄴ")], "weng": [F("ㅝ+ㅇ")], "wu": [F("ㅜ")], "wo": [F("ㅝ")],
    "yan": [F("ㅖ+ㄴ"), F("ㅕ+ㄴ"), F("ㅒ+ㄴ")], "you": [F("ㅠ"), F("ㅕ", "ㅜ")], "yi": [F("ㅣ")], "yo": [F("ㅛ")],
    "ye": [F("ㅖ")], "yong": [F("ㅠ+ㅇ")],
}
RETROFLEX = ("zh", "ch", "sh", "r", "z", "c", "s")


def split(py):
    for ini in ("zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l", "g", "k", "h", "j", "q", "x", "r", "z",
                "c", "s", "y", "w"):
        if py.startswith(ini):
            return ini, py[len(ini):]
    return "", py


def final_key(ini, fin):
    if ini in ("j", "q", "x", "y") and fin.startswith("u"):
        return "v" + fin[1:]            # ju = jü
    if ini == "y":
        return {"a": "ia", "ao": "iao", "e": "ie", "ou": "iu", "ang": "iang", "in": "in", "ing": "ing",
                "i": "i", "ong": "iong"}.get(fin, fin)
    if ini == "w":
        return {"a": "ua", "ai": "uai", "an": "uan", "ang": "uang", "o": "uo", "u": "u"}.get(fin, fin)
    return fin.replace("ü", "v")


def spellings(py):
    ini, fin = split(py)
    if py in SPECIAL:
        forms = SPECIAL[py]
    else:
        key = final_key(ini, fin)
        forms = list(FINALS.get(key, []))
        if fin == "i" and ini in RETROFLEX:
            forms = [F("ㅡ"), F("ㅣ")]          # zhi 즈 (also 지), si 쓰, ri 르
    out = []
    for c in INITIALS[ini]:
        for form in forms:
            parts = []
            for n, (v, j) in enumerate(form):
                parts.append(syl(c if n == 0 else "ㅇ", v, j))
            out.append("".join(parts))
    return out


def read_luna(path):
    chars, words = {}, {}
    body = False
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line == "...":
            body = True
            continue
        if not body or not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 2:
            continue
        text, py = f[0], f[1]
        w = float(f[2].rstrip("%")) if len(f) > 2 and f[2].endswith("%") else 100.0
        if len(text) == 1:
            chars.setdefault(text, []).append((w, py))
        else:
            words.setdefault(text, []).append(py)
    best = {c: max(r)[1] for c, r in chars.items()}
    return chars, best, words


def read_opencc(chars_path, phrases_path):
    cm, pm = {}, {}
    for path, m in ((chars_path, cm), (phrases_path, pm)):
        for line in open(path, encoding="utf-8"):
            if line.startswith("#") or "\t" not in line:
                continue
            k, v = line.rstrip("\n").split("\t", 1)
            m[k] = v.split(" ")[0]
    return cm, pm


def to_simp(text, cm, pm):
    if text in pm:
        return pm[text]
    return "".join(cm.get(c, c) for c in text)


def exclusions():
    """--exclude FILE: drop output lines matching any regex in FILE (one per line, '#' comments, any case)."""
    import re
    if "--exclude" not in sys.argv:
        return lambda line: False
    i = sys.argv.index("--exclude")
    path = sys.argv[i + 1]
    del sys.argv[i:i + 2]
    pats = [re.compile(l.strip(), re.I) for l in open(path, encoding="utf-8").read().splitlines()
            if l.strip() and not l.lstrip().startswith("#")]
    return lambda line: any(p.search(line) for p in pats)


def main():
    excluded = exclusions()
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    luna, essay, tsc, tsp = sys.argv[1:5]
    out_dir = sys.argv[5] if len(sys.argv) == 6 else "languages/zh/dict"
    chars, best, words = read_luna(luna)
    cm, pm = read_opencc(tsc, tsp)
    syllables = sorted({py for rs in chars.values() for _, py in rs if py.isalpha()})
    # map
    m = {}
    for py in syllables:
        for h in spellings(py):
            m.setdefault(h, [])
            if py not in m[h]:
                m[h].append(py)
    # Old letters narrow the reading (optional): the first syllable with an old initial, as conjoining jamo the way
    # the Hangul engine writes it (L + V + T): ᅎ ᅔ ᄼ = z c s, ᅐ ᅕ ᄾ = zh ch sh, ㅿ = r, ㆄ = f.
    old_initial = {"z": 0x114E, "c": 0x1154, "s": 0x113C, "zh": 0x1150, "ch": 0x1155, "sh": 0x113E, "r": 0x1140,
                   "f": 0x1157}
    for py in syllables:
        ini = split(py)[0]
        if ini not in old_initial:
            continue
        for h in spellings(py):
            code = ord(h[0]) - 0xAC00
            v, t = (code % 588) // 28, code % 28
            key = chr(old_initial[ini]) + chr(0x1161 + v) + (chr(0x11A7 + t) if t else "") + h[1:]
            m.setdefault(key, [])
            if py not in m[key]:
                m[key].append(py)
    with open(f"{out_dir}/zh.map", "w", encoding="utf-8", newline="\n") as f:
        f.write("# Hangul -> pinyin for rmpp-lang (tools/zh-dict.py)\n")
        for h in sorted(m):
            f.write(f"{h}\t{' '.join(m[h])}\n")
    # dictionary
    entries = {}

    def add(reading, word, weight):
        key = (reading, word)
        if entries.get(key, -1) < weight:
            entries[key] = weight

    freq = {}
    for line in open(essay, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) == 2 and f[1].isdigit():
            freq[f[0]] = int(f[1])
    for c, rs in chars.items():                      # every character, all its readings
        for w, py in rs:
            if py.isalpha():
                add(py, to_simp(c, cm, pm), freq.get(c, 0) * w / 100.0)
    for text, n in freq.items():                    # frequent words, readings from luna or from the characters
        if len(text) < 2 or n < MIN_WEIGHT:
            continue
        if text in words:
            pys = words[text]
        elif all(c in best for c in text):
            pys = [" ".join(best[c] for c in text)]
        else:
            continue
        for py in pys:
            reading = py.replace(" ", "")
            if reading.isalpha():
                add(reading, to_simp(text, cm, pm), n)
    rows = sorted(((r.encode(), -w, r, t) for (r, t), w in entries.items()))
    with open(f"{out_dir}/zh.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("# Chinese words by pinyin for rmpp-lang (tools/zh-dict.py)\n")
        f.write(f"# Sources: {PINNED}\n")
        f.write("# rime data: LGPL-3.0 (https://github.com/rime); OpenCC data: Apache-2.0\n")
        for _, nw, r, t in rows:
            line = f"{r}\t{t}\t\t{int(-nw)}"
            if not excluded(line):
                f.write(line + "\n")
    with open(f"{out_dir}/t2s.txt", "w", encoding="utf-8", newline="\n") as f:   # for the "hanja" method
        f.write("# Traditional -> Simplified, from OpenCC TSCharacters / TSPhrases (Apache-2.0)\n")
        for k, v in sorted(cm.items()):
            if k != v:
                f.write(f"{k}\t{v}\n")
        for k, v in sorted(pm.items()):
            f.write(f"{k}\t{v}\n")
    print(f"{out_dir}: zh.map {len(m)} spellings, zh.txt {len(rows)} entries, t2s.txt {len(cm) + len(pm)}")


if __name__ == "__main__":
    main()
