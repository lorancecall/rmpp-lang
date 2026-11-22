#!/usr/bin/env python3
"""Write the on-screen layouts of languages/bg (xochitl keyboard_layout.json) from the letter rows below.

Rows follow xkeyboard-config bg(bds), bg(bas_phonetic) and bg(phonetic); each layout holds the 30 letters.
Usage: python3 tools/bg-keyboards.py      (from the repository root)
"""
import json
from pathlib import Path

LONG = {'и': ['и', 'ѝ']}          # Ѝ by long press on И (a 31st key would not fit)
COMMA = [',', '„', '“', "'", '"', ':', ';', '/']   # „ “ by long press on the comma key
LAYOUTS = {
    # BDS 5237:2006 letter rows; Э (not a Bulgarian letter) only on physical keyboards
    'keyboards': ('уеишщксдзц', 'ьяаожгтнвмч', 'юйъфхпрлб'),
    # new phonetic (BDS 5237:2006): QWERTY rows, then ю (` key) and ь (\ key) at the end of the home row
    'keyboards@phonetic': ('чшертъуиопящ', 'асдфгхйклью', 'зжцвбнм'),
    # traditional phonetic: QWERTY rows, then ч (` key) and ю (\ key) at the end of the home row
    'keyboards@traditional': ('явертъуиопшщ', 'асдфгхйклчю', 'зьцжбнм'),
}


def key(c):
    d = LONG.get(c, [c])
    return {'default': d, 'shifted': [x.upper() for x in d]}


def layout(r1, r2, r3):
    alpha = [[key(c) for c in r1], [key(c) for c in r2],
             [{'special': 'shift'}] + [key(c) for c in r3] + [{'special': 'backspace'}]]
    letters = sorted(c for r in (r1, r2, r3) for c in r)
    assert len(letters) == 30 and len(set(letters)) == 30, letters
    spacebar = [{'special': 'language'}, {'special': 'alt'},
                {'default': COMMA, 'email': ['@'], 'shifted': COMMA},
                {'special': 'space'},
                {'default': ['.', '?', '!', '&', '(', ')'], 'email': ['.'],
                 'shifted': ['.', '?', '!', '&', '(', ')']},
                {'special': 'enter'}]
    return {'inherits': 'en_US', 'alphabetic': alpha, 'spacebarRow': spacebar}


for folder, rows in LAYOUTS.items():
    out = Path('languages/bg') / folder / 'bg_BG' / 'keyboard_layout.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(layout(*rows), ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(out)
