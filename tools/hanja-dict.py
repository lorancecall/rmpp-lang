#!/usr/bin/env python3
"""Build the Hanja dictionary of the ko add-on (languages/ko/hanja/hanja.txt) from libhangul's hanja.txt.

    python tools/hanja-dict.py SOURCE_hanja.txt [OUT]

SOURCE is data/hanja/hanja.txt of https://github.com/libhangul/libhangul (BSD-3-Clause, header kept in OUT).
Output: the license header (# lines), then one `reading:hanja:meaning` line per entry, sorted by the UTF-8 bytes of
the reading (the core binary-searches the memory-mapped file), within a reading: entries with a meaning first
(usually the common word, e.g. 韓國 before 寒國), then the source order.
"""
import sys

PINNED = "libhangul 717409ce61524bb3d8426060a384822f21354c62 data/hanja/hanja.txt"


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) == 3 else "languages/ko/hanja/hanja.txt"
    header, rows = [], []
    with open(src, encoding="utf-8") as f:
        for n, line in enumerate(f):
            line = line.rstrip("\n")
            if line.startswith("#"):
                if not rows:
                    header.append(line)
                continue
            parts = line.split(":")
            if len(parts) != 3 or not parts[0] or not parts[1]:
                continue
            reading, hanja, meaning = parts
            if any(not ("가" <= c <= "힣") for c in reading):
                continue  # readings are modern Hangul syllables only
            rows.append((reading.encode("utf-8"), 0 if meaning else 1, n, f"{reading}:{hanja}:{meaning}"))
    seen, lines = set(), []
    for r in sorted(rows):
        key = r[3].rsplit(":", 1)[0]
        if key not in seen:
            seen.add(key)
            lines.append(r[3])
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(header) + "\n")
        f.write(f"# Source: {PINNED}; sorted for rmpp-lang by tools/hanja-dict.py\n")
        f.write("\n".join(lines) + "\n")
    print(f"{out}: {len(lines)} entries")


if __name__ == "__main__":
    main()
