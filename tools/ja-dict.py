#!/usr/bin/env python3
"""Build the Japanese add-on's Hangul reading data (languages/ja/dict/): ja.map and ja.txt.

    python tools/ja-dict.py SKK-JISYO.L [OUT_DIR] [--exclude FILE]

ja.map: Hangul syllable -> kana readings (tab, then readings separated by spaces; the first is the strict one).
  Japanese is typed in Hangul (2-beolsik) and turned into kana by this table; the dictionary then decides which of
  the alternative readings exist (가 -> が or か, 도 -> ど, と, どう, とう, ...).
ja.txt: kana reading -> word, from SKK-JISYO.L (https://github.com/skk-dev/dict, GPL-2.0-or-later, header kept):
  `reading<TAB>word<TAB>note`, sorted by the reading's UTF-8 bytes, SKK order within a reading. Okuri-ari entries
  keep SKK's key (stem + romaji letter of the first ending kana, e.g. かk for 書く); the core adds the ending.
"""
import sys

PINNED = "skk-dev/dict 0a164e6b990c5eb5b59eb7d8789f08865dc2f644 SKK-JISYO.L"

# Hangul initial consonants (choseong index) and vowels (jungseong index)
CHO = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
JUNG = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
JONG = ["", "ㄱ", "ㄲ", "ㄳ", "ㄴ", "ㄵ", "ㄶ", "ㄷ", "ㄹ", "ㄺ", "ㄻ", "ㄼ", "ㄽ", "ㄾ", "ㄿ", "ㅀ", "ㅁ", "ㅂ", "ㅄ",
        "ㅅ", "ㅆ", "ㅇ", "ㅈ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"]


def syl(c, v, j=""):
    return chr(0xAC00 + (CHO.index(c) * 21 + JUNG.index(v)) * 28 + JONG.index(j))


# Kana rows: consonant jamo -> (kana for a i u e o), and alternatives that Korean spelling merges.
V5 = "ㅏㅣㅜㅔㅗ"
ROWS = {  # jamo: kana a, i, u, e, o ("" = none)
    "ㅇ": "あいうえお", "ㅋ": "かきくけこ", "ㄱ": "がぎぐげご", "ㅅ": "さしすせそ", "ㅈ": "ざじずぜぞ",
    "ㅌ": "たちつてと", "ㄷ": "だぢづでど", "ㄴ": "なにぬねの", "ㅎ": "はひふへほ", "ㅂ": "ばびぶべぼ",
    "ㅍ": "ぱぴぷぺぽ", "ㅁ": "まみむめも", "ㄹ": "らりるれろ",
}
# Korean spelling of Japanese writes voiceless sounds with plain consonants at the start of a word (도쿄 = とうきょう):
# plain consonants also try the voiceless kana.
VOICELESS = {"ㄱ": "ㅋ", "ㄷ": "ㅌ", "ㅈ": "ㅊ"}
# Contracted sounds (yōon): ㅑ ㅠ ㅛ after a consonant
YOON = {"ㅋ": "き", "ㄱ": "ぎ", "ㅅ": "し", "ㅈ": "じ", "ㅊ": "ち", "ㄴ": "に", "ㅎ": "ひ", "ㅂ": "び", "ㅍ": "ぴ",
        "ㅁ": "み", "ㄹ": "り"}
SMALL = {"ㅑ": "ゃ", "ㅠ": "ゅ", "ㅛ": "ょ"}
LONG = {"a": "あ", "i": "い", "u": "う", "e": "い", "o": "う"}   # long vowel spelled in kana (おう, えい)
VOWEL_OF = dict(zip(V5, "aiueo"))
VOICED_KANA = set("がぎぐげござじずぜぞだぢづでど")


def base_readings():
    """Hangul syllable without final -> [(kana, vowel letter)], strict first."""
    out = {}

    def add(s, kana, vowel):
        out.setdefault(s, [])
        if (kana, vowel) not in out[s]:
            out[s].append((kana, vowel))

    for c, kana in ROWS.items():
        for v, k in zip(V5, kana):
            add(syl(c, v), k, VOWEL_OF[v])
    for c, vl in VOICELESS.items():           # 가 -> が, also か; 지 -> じ, also ち
        for v in V5:
            if c == "ㅈ":
                add(syl(c, v), {"ㅏ": "ちゃ", "ㅣ": "ち", "ㅜ": "ちゅ", "ㅔ": "ちぇ", "ㅗ": "ちょ"}[v], VOWEL_OF[v])
            else:
                add(syl(c, v), ROWS[vl]["ㅏㅣㅜㅔㅗ".index(v)], VOWEL_OF[v])
    # ち / つ: 치, 쓰, 츠; ふ also 후 (done); じゃ じゅ じょ also 자 주 조; ちゃ ちゅ ちょ also 차 추 초
    add(syl("ㅊ", "ㅣ"), "ち", "i")
    for s in (syl("ㅆ", "ㅡ"), syl("ㅊ", "ㅡ"), syl("ㅉ", "ㅡ")):
        add(s, "つ", "u")
    add(syl("ㅅ", "ㅡ"), "す", "u")
    add(syl("ㅈ", "ㅡ"), "ず", "u")
    add(syl("ㅈ", "ㅡ"), "づ", "u")
    add(syl("ㄱ", "ㅡ"), "ぐ", "u")
    add(syl("ㅋ", "ㅡ"), "く", "u")
    add(syl("ㄷ", "ㅡ"), "ど", "o")      # ド is often written 드 in loanwords
    add(syl("ㅌ", "ㅡ"), "と", "o")
    add(syl("ㅎ", "ㅡ"), "ふ", "u")
    add(syl("ㄹ", "ㅡ"), "る", "u")
    add(syl("ㅂ", "ㅡ"), "ぶ", "u")
    add(syl("ㅍ", "ㅡ"), "ぷ", "u")
    add(syl("ㅁ", "ㅡ"), "む", "u")
    add(syl("ㄴ", "ㅡ"), "ぬ", "u")
    for c, a, u, o in (("ㅈ", "じゃ", "じゅ", "じょ"), ("ㅊ", "ちゃ", "ちゅ", "ちょ")):
        add(syl(c, "ㅏ"), a, "a"); add(syl(c, "ㅜ"), u, "u"); add(syl(c, "ㅗ"), o, "o")
        add(syl(c, "ㅑ"), a, "a"); add(syl(c, "ㅠ"), u, "u"); add(syl(c, "ㅛ"), o, "o")
    add(syl("ㅊ", "ㅔ"), "ちぇ", "e")
    add(syl("ㅈ", "ㅔ"), "じぇ", "e")
    for c, i in YOON.items():                  # 캬 きゃ, 큐 きゅ, 쿄 きょ, ...
        for v, sm in SMALL.items():
            add(syl(c, v), i + sm, {"ㅑ": "a", "ㅠ": "u", "ㅛ": "o"}[v])
    for c, vl in VOICELESS.items():
        if c in YOON and vl in YOON:
            for v, sm in SMALL.items():
                add(syl(c, v), YOON[vl] + sm, {"ㅑ": "a", "ㅠ": "u", "ㅛ": "o"}[v])
    for v, k, vow in (("ㅑ", "や", "a"), ("ㅠ", "ゆ", "u"), ("ㅛ", "よ", "o")):
        add(syl("ㅇ", v), k, vow)
    add(syl("ㅇ", "ㅘ"), "わ", "a")
    add(syl("ㅇ", "ㅝ"), "を", "o")
    # particles as they sound: 와 = は, 에 = へ, 오 = を
    add(syl("ㅇ", "ㅘ"), "は", "a")
    add(syl("ㅇ", "ㅔ"), "へ", "e")
    add(syl("ㅇ", "ㅗ"), "を", "o")
    # ふぁ ふぃ ふぇ ふぉ, てぃ, でぃ, うぃ うぇ うぉ (loanwords)
    add(syl("ㅍ", "ㅘ"), "ふぁ", "a"); add(syl("ㅎ", "ㅘ"), "ふぁ", "a")
    add(syl("ㅍ", "ㅟ"), "ふぃ", "i"); add(syl("ㅍ", "ㅞ"), "ふぇ", "e"); add(syl("ㅍ", "ㅝ"), "ふぉ", "o")
    add(syl("ㅌ", "ㅣ"), "てぃ", "i"); add(syl("ㄷ", "ㅣ"), "でぃ", "i")
    add(syl("ㅇ", "ㅟ"), "うぃ", "i"); add(syl("ㅇ", "ㅞ"), "うぇ", "e")
    add(syl("ㅂ", "ㅘ"), "ゔぁ", "a")
    return out


def build_map():
    base = base_readings()
    nasal, stop = ("ㄴ", "ㅇ", "ㅁ"), ("ㅅ", "ㄱ", "ㄷ", "ㅂ")
    out = {}
    for s, readings in base.items():
        code = ord(s) - 0xAC00
        c, v = CHO[code // 588], JUNG[(code % 588) // 28]
        for jong in [""] + list(nasal) + list(stop):
            t = syl(c, v, jong)
            alts = []
            for kana, vowel in readings:
                end = "ん" if jong in nasal else "っ" if jong in stop else ""
                for r in (kana + end, kana + LONG[vowel] + end):
                    if r not in alts:
                        alts.append(r)
            out.setdefault(t, [])
            out[t] += [a for a in alts if a not in out[t]]
    return out


def okuri_ok(cand):
    return cand and not cand.startswith("(") and ":" not in cand and "\t" not in cand


def build_dict(src):
    raw = open(src, "rb").read().decode("euc_jp", errors="replace")
    header, rows, n = [], [], 0
    for line in raw.split("\n"):
        if line.startswith(";;"):
            if not rows and n < 40:
                header.append("#" + line[2:])
                n += 1
            continue
        if " /" not in line:
            continue
        reading, rest = line.split(" /", 1)
        if not reading or any(c in reading for c in " \t:>#"):
            continue
        for item in rest.rstrip("/").split("/"):
            word, _, note = item.partition(";")
            if not okuri_ok(word):
                continue
            note = note.replace("\t", " ").replace(":", " ")[:24]
            rows.append((reading.encode("utf-8"), len(rows), f"{reading}\t{word}\t{note}"))
    rows.sort()
    header = [h for h in header if "coding:" not in h]
    return header, [r[2] for r in rows]


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


def build_romaji():
    """Romaji (Hepburn and kunrei-shiki, as Japanese IMEs accept) -> kana, for the ja "romaji" input method."""
    base = {}
    rows = {"": "あいうえお", "k": "かきくけこ", "g": "がぎぐげご", "s": "さしすせそ", "z": "ざじずぜぞ",
            "t": "たちつてと", "d": "だぢづでど", "n": "なにぬねの", "h": "はひふへほ", "b": "ばびぶべぼ",
            "p": "ぱぴぷぺぽ", "m": "まみむめも", "r": "らりるれろ", "y": "や\0ゆいぇよ", "w": "わうぃう\0うぇを"}
    for c, kana in rows.items():
        for v, k in zip("aiueo", kana.replace("いぇ", "\x01").replace("うぃ", "\x02").replace("うぇ", "\x03")):
            k = {"\x01": "いぇ", "\x02": "うぃ", "\x03": "うぇ"}.get(k, k)
            if k != "\0":
                base[c + v] = k
    base.update({"shi": "し", "chi": "ち", "tsu": "つ", "fu": "ふ", "ji": "じ", "wo": "を", "nn": "ん", "n'": "ん",
                 "n": "ん", "-": "ー", "fa": "ふぁ", "fi": "ふぃ", "fe": "ふぇ", "fo": "ふぉ", "vu": "ゔ",
                 "va": "ゔぁ", "vi": "ゔぃ", "ve": "ゔぇ", "vo": "ゔぉ", "thi": "てぃ", "dhi": "でぃ", "che": "ちぇ",
                 "she": "しぇ", "je": "じぇ", "xtu": "っ", "ltu": "っ"})
    for c, i in (("k", "き"), ("g", "ぎ"), ("s", "し"), ("z", "じ"), ("t", "ち"), ("d", "ぢ"), ("n", "に"),
                 ("h", "ひ"), ("b", "び"), ("p", "ぴ"), ("m", "み"), ("r", "り")):
        for v, sm in (("a", "ゃ"), ("u", "ゅ"), ("o", "ょ")):
            base[c + "y" + v] = i + sm
    for c, i in (("sh", "し"), ("ch", "ち"), ("j", "じ"), ("cy", "ち")):
        for v, sm in (("a", "ゃ"), ("u", "ゅ"), ("o", "ょ")):
            base[c + v] = i + sm
    for v, k in zip("aiueo", "ぁぃぅぇぉ"):
        base["x" + v] = k; base["l" + v] = k
    out = {k: [v] for k, v in base.items()}
    for k, v in list(base.items()):          # doubled consonant = っ (kka = っか), not for n
        if k[0] not in "aiueon-'x" and len(k) >= 2:
            out.setdefault(k[0] + k, []).append("っ" + v)
    return out


def main():
    excluded = exclusions()
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    out_dir = sys.argv[2] if len(sys.argv) == 3 else "languages/ja/dict"
    m = build_map()
    with open(f"{out_dir}/ja.map", "w", encoding="utf-8", newline="\n") as f:
        f.write("# Hangul -> kana readings for rmpp-lang (tools/ja-dict.py); first = strict\n")
        for s in sorted(m):
            f.write(f"{s}\t{' '.join(m[s])}\n")
        # ^syllable: order at the start of a word, where Korean spelling writes voiceless sounds with plain
        # consonants (가이샤 = かいしゃ): voiceless kana first there.
        for s in sorted(m):
            if CHO[(ord(s) - 0xAC00) // 588] in VOICELESS:
                voiced = [r for r in m[s] if r[0] in VOICED_KANA]
                rest = [r for r in m[s] if r[0] not in VOICED_KANA]
                if voiced and rest:
                    f.write(f"^{s}\t{' '.join(rest + voiced)}\n")
    with open(f"{out_dir}/romaji.map", "w", encoding="utf-8", newline="\n") as f:
        f.write("# Romaji -> kana for rmpp-lang (tools/ja-dict.py), the ja \"romaji\" input method\n")
        for k, v in sorted(build_romaji().items()):
            f.write(f"{k}\t{' '.join(v)}\n")
    header, lines = build_dict(sys.argv[1])
    with open(f"{out_dir}/ja.txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header) + "\n")
        f.write(f"# Source: {PINNED}; converted for rmpp-lang by tools/ja-dict.py\n")
        f.write("\n".join(l for l in lines if not excluded(l)) + "\n")
    print(f"{out_dir}: ja.map {len(m)} syllables, ja.txt {len(lines)} entries")


if __name__ == "__main__":
    main()
