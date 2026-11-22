#!/usr/bin/env python3
"""Stroke glyph files of the language add-ons: pen strokes for text that rmpp-hwr writes on a page as ink
("Add text", Convert to ink). Each add-on gets languages/<code>/glyphs.rmgl, the centre lines of its font.

Method (from rmpp-hwr ink/glyphs.py, lorancecall/rmpp-hwr f0abc3c, GPL-3.0-only like this repository): render a
character at SIZE px, thin it to 1 px (Zhang-Suen), trace the skeleton into a stroke graph, drop short spurs, join
strokes that run straight on through a junction, simplify (Ramer-Douglas-Peucker).

  glyphs.py build CODE... [--jobs N]   language.conf glyph_font= / glyph_chars= -> languages/<code>/glyphs.rmgl
  glyphs.py preview FILE "text" -o out.png
  glyphs.py info FILE [CHARS]

Fonts (tools/glyphs/fonts.conf: name, URL, SHA-256) are downloaded to build/glyph-fonts/ and checked. They are
SIL OFL 1.1 (tools/glyphs/OFL.txt); the notice of the font goes into the file's metadata. File format: docs/DESIGN.md, "Glyph files".
"""
import argparse
import hashlib
import json
import struct
import sys
import urllib.request
from multiprocessing import Pool
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
BASELINE = 0.82   # em fraction where the baseline sits
SIZE = 160        # raster size for thinning
UNITS = 4096      # file coordinates: int16 in 1/UNITS em
MAGIC, VERSION = b"RMGL", 1


# ---------------------------------------------------------------- character sets
def _dbcs(codec, rows, cols=range(0xA1, 0xFF)):
    out = []
    for hi in rows:
        for lo in cols:
            try:
                out.append(bytes([hi, lo]).decode(codec))
            except UnicodeDecodeError:
                pass
    return out


def charset(spec):
    out = []
    for part in spec.split(","):
        part = part.strip()
        if part == "ascii":
            out += [chr(c) for c in range(0x21, 0x7F)]
        elif part == "punct":            # common non-ASCII punctuation and symbols
            out += list("–—‘’‚“”„…«»‹›·•№§°±×÷€£¥©®™")
        elif part == "cyrillic":         # U+0400-045F + the extra letters of Cyrillic-script languages
            out += [chr(c) for c in range(0x0400, 0x0460)] + [chr(c) for c in range(0x0490, 0x04C0)]
        elif part == "gb2312":           # GB 2312: symbols (rows 1-9) and the 6,763 hanzi (rows 16-87)
            out += _dbcs("gb2312", list(range(0xA1, 0xAA)) + list(range(0xB0, 0xF8)))
        elif part == "jisx0208":         # JIS X 0208: symbols, kana, level 1 and 2 kanji
            out += _dbcs("euc_jp", range(0xA1, 0xF5))
        elif part == "kana":
            out += [chr(c) for c in range(0x3041, 0x3097)] + [chr(c) for c in range(0x30A1, 0x30FB)] + list("ー・")
        elif part == "ksx1001":          # KS X 1001: symbols, jamo, 2,350 Hangul, 4,888 hanja
            out += _dbcs("euc_kr", range(0xA1, 0xFE))
        elif part == "hangul":           # all 11,172 modern syllables + compatibility jamo
            out += [chr(c) for c in range(0xAC00, 0xD7A4)] + [chr(c) for c in range(0x3131, 0x318F)]
        elif part.startswith("U+"):      # U+XXXX-YYYY
            a, _, b = part[2:].partition("-")
            out += [chr(c) for c in range(int(a, 16), int(b or a, 16) + 1)]
        elif part:
            raise SystemExit(f"unknown character set: {part}")
    return [c for c in dict.fromkeys(out) if not c.isspace()]


# ---------------------------------------------------------------- thinning + tracing (as in rmpp-hwr)
def thin(img):
    """Zhang-Suen thinning of a boolean image (True = ink)."""
    im = np.pad(img.astype(np.uint8), 1)
    changed = True
    while changed:
        changed = False
        for step in (0, 1):
            p = [np.roll(np.roll(im, -dy, 0), -dx, 1) for dy, dx in
                 ((-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (1, -1), (0, -1), (-1, -1))]   # P2..P9
            b = sum(p)
            seq = p + [p[0]]
            a = sum(((seq[i] == 0) & (seq[i + 1] == 1)).astype(np.uint8) for i in range(8))
            if step == 0:
                c = (p[0] * p[2] * p[4] == 0) & (p[2] * p[4] * p[6] == 0)
            else:
                c = (p[0] * p[2] * p[6] == 0) & (p[0] * p[4] * p[6] == 0)
            m = (im == 1) & (b >= 2) & (b <= 6) & (a == 1) & c
            if m.any():
                im[m] = 0
                changed = True
    return im[1:-1, 1:-1].astype(bool)


NB = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]


def skeleton_graph(sk):
    """Skeleton pixels -> (nodes, edges). Neighbouring junction/end pixels (degree != 2) form one node at their
    centroid; an edge is the chain of degree-2 pixels between two nodes, as a polyline that starts and ends at the
    node centres (so strokes meet at junctions instead of stopping short). Closed loops become a node-less edge."""
    pts = set(zip(*np.nonzero(sk)))
    nbrs = {p: [(p[0] + dy, p[1] + dx) for dy, dx in NB if (p[0] + dy, p[1] + dx) in pts] for p in pts}
    special = {p for p in pts if len(nbrs[p]) != 2}
    node_of, nodes = {}, []
    for p in special:   # group touching special pixels
        if p in node_of:
            continue
        stack, comp = [p], []
        node_of[p] = len(nodes)
        while stack:
            q = stack.pop()
            comp.append(q)
            for r in nbrs[q]:
                if r in special and r not in node_of:
                    node_of[r] = len(nodes)
                    stack.append(r)
        c = np.mean(comp, axis=0)
        nodes.append((c[1], c[0]))   # x, y
    edges, seen = [], set()
    for p in special:
        for q in nbrs[p]:
            if q in special:
                if node_of[q] != node_of[p] and (q, p) not in seen:   # two nodes directly adjacent
                    seen.add((p, q))
                    edges.append([node_of[p], node_of[q], [nodes[node_of[p]], nodes[node_of[q]]]])
                continue
            if (p, q) in seen:
                continue
            path, prev, cur = [nodes[node_of[p]]], p, q
            while cur not in special:
                path.append((cur[1], cur[0]))
                nxt = [r for r in nbrs[cur] if r != prev]
                if not nxt:
                    break
                prev, cur = cur, nxt[0]
            seen.add((cur, prev))   # the same edge walked from the other end
            seen.add((p, q))
            if cur in special:
                path.append(nodes[node_of[cur]])
                edges.append([node_of[p], node_of[cur], path])
    done = set(special) | {(int(round(y)), int(round(x))) for e in edges for x, y in e[2]}
    for p in sorted(pts - done):   # loops without any junction (O, ㅇ)
        if p in done:
            continue
        path, prev, cur = [(p[1], p[0])], None, p
        done.add(p)
        while True:
            nxt = [r for r in nbrs[cur] if r != prev and r not in done]
            if not nxt:
                break
            prev, cur = cur, nxt[0]
            done.add(cur)
            path.append((cur[1], cur[0]))
        path.append(path[0])
        edges.append([None, None, path])
    return nodes, edges


def trace(sk, size):
    """Skeleton -> strokes: drop short end spurs (thinning artefacts), then chain edges through every node where the
    stroke continues: always at a 2-way node (corner), and at junctions for the straightest pair."""
    nodes, edges = skeleton_graph(sk)
    deg = lambda n: sum((e[0] == n) + (e[1] == n) for e in edges)
    length = lambda pl: float(np.sum(np.hypot(*np.diff(np.array(pl), axis=0).T))) if len(pl) > 1 else 0.0
    spur = 0.04 * size
    changed = True
    while changed:   # remove spurs: an edge to a dead end that is short, when its other end is a junction
        changed = False
        for e in list(edges):
            a, b = e[0], e[1]
            if a is None:
                continue
            for end, other in ((a, b), (b, a)):
                if deg(end) == 1 and deg(other) >= 3 and length(e[2]) < spur:
                    edges.remove(e)
                    changed = True
                    break
            if changed:
                break
    strokes = [[e[0], e[1], [np.array(p) for p in e[2]]] for e in edges]

    def direction(path, at_start):
        p = np.array(path if at_start else path[::-1])
        k = min(len(p) - 1, 6)
        d = p[k] - p[0]
        n = np.hypot(*d)
        return d / n if n else d

    merged = True
    while merged:
        merged = False
        for n in range(len(nodes)):
            ends = [(i, s[0] == n) for i, s in enumerate(strokes) if s[0] == n] + \
                   [(i, False) for i, s in enumerate(strokes) if s[1] == n and s[0] != n]
            if len(ends) < 2:
                continue
            best = None
            for x in range(len(ends)):
                for y in range(x + 1, len(ends)):
                    (i, si), (j, sj) = ends[x], ends[y]
                    if i == j:
                        continue
                    c = float(np.dot(direction(strokes[i][2], si), direction(strokes[j][2], sj)))   # -1 = straight on
                    if best is None or c < best[0]:
                        best = (c, ends[x], ends[y])
            if best is None or (len(ends) > 2 and best[0] > -0.7):
                continue   # at a junction only join a clearly straight continuation
            (c, (i, si), (j, sj)) = best
            A = strokes[i][2][::-1] if si else strokes[i][2]          # ends at n
            B = strokes[j][2] if sj else strokes[j][2][::-1]          # starts at n
            a_other = strokes[i][1] if si else strokes[i][0]
            b_other = strokes[j][1] if sj else strokes[j][0]
            new = [a_other, b_other, A + B[1:]]
            for k in sorted((i, j), reverse=True):
                del strokes[k]
            strokes.append(new)
            merged = True
            break
    out = [np.array(s[2], dtype=float) for s in strokes]
    used = {s[0] for s in strokes} | {s[1] for s in strokes}
    for n, (x, y) in enumerate(nodes):   # isolated dots (a skeleton of one pixel cluster)
        if n not in used:
            out.append(np.array([(x - 0.5, y), (x + 0.5, y)]))
    return out


def rdp(p, eps):
    if len(p) < 3:
        return p
    a, b = p[0], p[-1]
    d = b - a
    n = np.hypot(*d)
    dist = np.abs(d[0] * (p[:, 1] - a[1]) - d[1] * (p[:, 0] - a[0])) / n if n > 0 else np.hypot(*(p - a).T)
    i = int(np.argmax(dist))
    if dist[i] > eps:
        return np.vstack([rdp(p[:i + 1], eps)[:-1], rdp(p[i:], eps)])
    return np.array([a, b])


_font = None


def _init(font_path):
    global _font
    _font = ImageFont.truetype(font_path, SIZE)
    try:                                   # variable fonts: the Regular instance
        axes = _font.get_variation_axes()
        _font.set_variation_by_axes([400 if a.get("name", b"") in (b"Weight", "Weight") else a["default"]
                                     for a in axes])
    except (OSError, AttributeError):
        pass


def font_glyph(ch):
    """One character -> (advance, strokes) in em units (y down, 0 = top of the em box)."""
    img = Image.new("L", (SIZE * 2, SIZE * 2), 0)
    ImageDraw.Draw(img).text((SIZE // 2, SIZE // 2 + BASELINE * SIZE), ch, font=_font, fill=255, anchor="ls")
    adv = _font.getlength(ch) / SIZE
    a = np.array(img) > 110
    strokes = []
    if a.any():
        sk = thin(a)
        for l in trace(sk, SIZE):
            strokes.append((rdp(l, SIZE / 140) - SIZE / 2) / SIZE)
        for y, x in lost_dots(a, sk):   # small dots that thinning removed entirely (·, …, the dots of ё)
            strokes.append((np.array([(x - 0.5, y), (x + 0.5, y)]) - SIZE / 2) / SIZE)
    return ch, adv, strokes


def lost_dots(ink, sk):
    """Centres of the ink components that have no skeleton pixel left (not in rmpp-hwr's method: added here)."""
    seen = np.zeros_like(ink)
    out = []
    h, w = ink.shape
    for y0, x0 in zip(*np.nonzero(ink)):
        if seen[y0, x0]:
            continue
        seen[y0, x0] = True
        stack, comp, keep = [(y0, x0)], [], False
        while stack:
            y, x = stack.pop()
            comp.append((y, x))
            keep = keep or sk[y, x]
            for dy, dx in NB:
                v, u = y + dy, x + dx
                if 0 <= v < h and 0 <= u < w and ink[v, u] and not seen[v, u]:
                    seen[v, u] = True
                    stack.append((v, u))
        if not keep:
            out.append(tuple(np.mean(comp, axis=0)))
    return out


# ---------------------------------------------------------------- file format (docs/DESIGN.md, "Glyph files")
def q(v):
    return max(-32768, min(32767, int(round(v * UNITS))))


def split255(s):
    """A stroke as runs of at most 255 points (consecutive runs share their joining point)."""
    s = np.asarray(s)
    return [s[k:k + 255] for k in range(0, max(len(s) - 1, 1), 254)]


def write_rmgl(path, glyphs, meta, baseline=BASELINE):
    """glyphs: {char: (adv, [ndarray (n, 2) em])} -> RMGL v1, little-endian (layout agreed with rmpp-hwr)."""
    meta_b = json.dumps(meta, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    chars = sorted(glyphs, key=ord)
    index_off = (20 + len(meta_b) + 3) & ~3
    off = index_off + 8 * len(chars)
    index, data = bytearray(), bytearray()
    for ch in chars:
        adv, strokes = glyphs[ch]
        runs = [r for s in strokes for r in split255(s) if len(r)]
        if len(runs) > 255:
            raise SystemExit(f"U+{ord(ch):04X}: {len(runs)} strokes (> 255)")
        index += struct.pack("<II", ord(ch), off + len(data))
        data += struct.pack("<HB", max(0, min(65535, int(round(adv * UNITS)))), len(runs))
        for r in runs:
            data += struct.pack("<B", len(r)) + struct.pack(f"<{2 * len(r)}h", *(q(v) for v in r.reshape(-1)))
    out = struct.pack("<4sHHIfI", MAGIC, VERSION, 0, len(chars), baseline, len(meta_b)) + meta_b
    out += b"\0" * (index_off - len(out)) + index + data
    Path(path).write_bytes(out)
    return len(out)


def read_rmgl(path):
    b = Path(path).read_bytes()
    magic, ver, flags, count, base, ml = struct.unpack_from("<4sHHIfI", b, 0)
    if magic != MAGIC or ver != VERSION:
        raise SystemExit(f"{path}: not an RMGL v{VERSION} file")
    meta = json.loads(b[20:20 + ml])
    io = (20 + ml + 3) & ~3
    glyphs = {}
    for i in range(count):
        cp, p = struct.unpack_from("<II", b, io + 8 * i)
        adv, n = struct.unpack_from("<HB", b, p)
        p += 3
        strokes = []
        for _ in range(n):
            k = b[p]
            p += 1
            strokes.append(np.array(struct.unpack_from(f"<{2 * k}h", b, p), dtype=float).reshape(-1, 2) / UNITS)
            p += 4 * k
        glyphs[chr(cp)] = (adv / UNITS, strokes)
    return {"baseline": base, "meta": meta, "glyphs": glyphs}


# ---------------------------------------------------------------- build
def read_conf(path):
    kv = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            kv[k.strip()] = v.split(" #")[0].strip()
    return kv


def font_file(name):
    """Font `name` from tools/glyphs/fonts.conf, downloaded once to build/glyph-fonts/ and checked."""
    for line in (ROOT / "tools/glyphs/fonts.conf").read_text(encoding="utf-8").splitlines():
        f = line.split()
        if len(f) >= 4 and not line.startswith("#") and f[0] == name:
            url, sha, notice = f[1], f[2], " ".join(f[3:])
            break
    else:
        raise SystemExit(f"font {name} is not in tools/glyphs/fonts.conf")
    out = ROOT / "build/glyph-fonts" / (name + Path(url.split("?")[0]).suffix)
    if not out.exists() or hashlib.sha256(out.read_bytes()).hexdigest() != sha:
        out.parent.mkdir(parents=True, exist_ok=True)
        print(f"download {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, out)
        if hashlib.sha256(out.read_bytes()).hexdigest() != sha:
            raise SystemExit(f"{out}: SHA-256 mismatch")
    return out, notice


def cmd_build(a):
    for code in a.codes:
        conf = read_conf(ROOT / "languages" / code / "language.conf")
        if not conf.get("glyph_font") or not conf.get("glyph_chars"):
            print(f"{code}: no glyph_font / glyph_chars, skipped", file=sys.stderr)
            continue
        path, notice = font_file(conf["glyph_font"])
        _init(str(path))
        chars = charset(conf["glyph_chars"])
        try:
            from fontTools.ttLib import TTFont
            have = set(TTFont(str(path), lazy=True).getBestCmap())
            missing = [c for c in chars if ord(c) not in have]
            chars = [c for c in chars if ord(c) in have]
        except ImportError:
            missing = []
        glyphs = {}
        with Pool(a.jobs, initializer=_init, initargs=(str(path),)) as pool:
            for n, (ch, adv, strokes) in enumerate(pool.imap_unordered(font_glyph, chars, chunksize=32)):
                glyphs[ch] = (adv, strokes)
                if n % 1000 == 0:
                    print(f"{code}: {n}/{len(chars)}", file=sys.stderr)
        glyphs[" "] = (_font.getlength(" ") / SIZE or 0.3, [])
        meta = {"lang": code, "font": conf["glyph_font"], "notice": notice, "chars": conf["glyph_chars"],
                "source": f"rmpp-lang tools/glyphs/glyphs.py, centre lines at {SIZE} px"}
        out = ROOT / "languages" / code / "glyphs.rmgl"
        n = write_rmgl(out, glyphs, meta)
        print(f"{out.relative_to(ROOT)}: {len(glyphs)} glyphs, {n} bytes"
              + (f" ({len(missing)} not in the font)" if missing else ""))


# ---------------------------------------------------------------- preview / info
def layout(g, text, h, track=0.04):
    x, out = 0.0, []
    for ch in text:
        gl = g["glyphs"].get(ch) or g["glyphs"].get("?")
        if gl is None:
            x += 0.5
            continue
        adv, strokes = gl
        out += [np.column_stack([(s[:, 0] + x) * h, s[:, 1] * h]) for s in strokes]
        x += adv + track
    return out, x * h


def cmd_preview(a):
    g = read_rmgl(a.file)
    h, rows = 80, a.text.split("\\n")
    res = [layout(g, r, h) for r in rows]
    W = int(max(w for _, w in res)) + 40
    img = Image.new("L", (W, int(len(rows) * h * 1.25) + 40), 255)
    d = ImageDraw.Draw(img)
    for i, (strokes, _) in enumerate(res):
        oy = 20 + i * h * 1.25
        d.line([(20, oy + g["baseline"] * h), (W - 20, oy + g["baseline"] * h)], fill=210)
        for s in strokes:
            d.line([(float(x) + 20, float(y) + oy) for x, y in s], fill=0, width=3, joint="curve")
    img.save(a.out)
    print(a.out)


def cmd_info(a):
    g = read_rmgl(a.file)
    pts = sum(len(s) for _, ss in g["glyphs"].values() for s in ss)
    print(json.dumps(g["meta"], ensure_ascii=False))
    print(f"{len(g['glyphs'])} glyphs, {pts} points, baseline {g['baseline']}")
    for ch in a.chars or "":
        adv, ss = g["glyphs"].get(ch, (None, []))
        print(ch, adv, [len(s) for s in ss])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("codes", nargs="+")
    b.add_argument("--jobs", type=int, default=None)
    p = sp.add_parser("preview")
    p.add_argument("file")
    p.add_argument("text")
    p.add_argument("-o", "--out", required=True)
    i = sp.add_parser("info")
    i.add_argument("file")
    i.add_argument("chars", nargs="?")
    a = ap.parse_args()
    {"build": cmd_build, "preview": cmd_preview, "info": cmd_info}[a.cmd](a)


if __name__ == "__main__":
    main()
