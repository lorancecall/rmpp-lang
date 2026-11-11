#!/usr/bin/env python3
"""rmpp-lang from your computer: install fonts per language on the tablet and run the tablet's rmpp-lang command.

  python rmpp-lang.py [--host rmpp] font add MyFont.ttf [More.otf ...] --lang ko [--use]
  python rmpp-lang.py [--host rmpp] font get "Nanum Myeongjo" --lang ko [--use]   (Google Fonts, OFL only)
  python rmpp-lang.py [--host rmpp] status | lang on ja | font use "Noto Sans CJK JP" --lang ja | probe ...

Needs ssh/scp access to the tablet (developer mode). HOST is an ssh host name or alias (default: rmpp).
"""
import json
import os
import shlex
import subprocess
import sys
import tempfile
import urllib.request

CODES = ("ko", "en", "ja", "zh", "latin")
FONT_EXT = (".ttf", ".otf", ".ttc")


def run(args, **kw):
    return subprocess.run(args, check=True, **kw)


def tablet(host, args):
    """Run the tablet's rmpp-lang with these arguments."""
    return run(["ssh", host, "rmpp-lang " + " ".join(shlex.quote(a) for a in args)]).returncode


def upload_and_add(host, files, rest):
    remote = []
    for f in files:
        if not os.path.isfile(f) or not f.lower().endswith(FONT_EXT):
            sys.exit(f"not a font file: {f}")
        name = os.path.basename(f)
        # scp reads "C:" in a Windows path as a host name: copy from the file's folder with a relative name.
        run(["scp", "-q", name, f"{host}:/tmp/{name}"], cwd=os.path.dirname(os.path.abspath(f)))
        remote.append(f"/tmp/{name}")
    code = tablet(host, ["font", "add", *remote, *rest])
    run(["ssh", host, "rm -f " + " ".join(shlex.quote(r) for r in remote)])
    return code


def google_font(family, out):
    """Download an OFL family from the google/fonts repository. Returns the downloaded font files."""
    slug = "".join(ch for ch in family.lower() if ch.isalnum())
    api = f"https://api.github.com/repos/google/fonts/contents/ofl/{slug}"
    req = urllib.request.Request(api, headers={"User-Agent": "rmpp-lang", "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            entries = json.load(r)
    except Exception as e:  # noqa: BLE001
        sys.exit(f"'{family}' not found under ofl/ in google/fonts ({e}); only SIL OFL fonts are fetched")
    files = []
    for e in entries:
        name = e.get("name", "")
        if name.lower().endswith(FONT_EXT) or name == "OFL.txt":
            path = os.path.join(out, name)
            urllib.request.urlretrieve(e["download_url"], path)
            if name != "OFL.txt":
                files.append(path)
    if not files:
        sys.exit(f"no font files in ofl/{slug}")
    return files


def main(argv):
    host = os.environ.get("RMPP_HOST", "rmpp")
    if len(argv) >= 2 and argv[0] == "--host":
        host, argv = argv[1], argv[2:]
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    if argv[:2] == ["font", "add"]:
        files = [a for a in argv[2:] if a.lower().endswith(FONT_EXT)]
        rest = [a for a in argv[2:] if a not in files]
        return upload_and_add(host, files, rest)
    if argv[:2] == ["font", "get"] and len(argv) >= 3:
        if "--lang" not in argv:
            sys.exit("font get needs --lang CODE")
        with tempfile.TemporaryDirectory() as tmp:
            files = google_font(argv[2], tmp)
            print("downloaded: " + ", ".join(os.path.basename(f) for f in files))
            return upload_and_add(host, files, argv[3:])
    return tablet(host, argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
