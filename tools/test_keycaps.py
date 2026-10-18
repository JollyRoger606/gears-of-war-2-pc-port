"""Checks of the keycap tooling that need no game data (font_info.json holds only numbers)."""
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "keycaps"))

import keys  # noqa: E402
import pack  # noqa: E402
import svgkit  # noqa: E402

# Every name the runtime's key parser accepts (src/ui/keybinds.cpp of the SDK).
SDK_KEY_NAMES = (
    [f"F{n}" for n in range(1, 25)] + list("ABCDEFGHIJKLMNOPQRSTUVWXYZ") + [str(d) for d in range(10)]
    + ["Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket",
       "RBracket", "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert", "Home",
       "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down", "Shift", "Control", "Alt"]
    + [f"Numpad{d}" for d in range(10)]
    + ["NumpadEnter", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash", "PrintScreen", "Pause",
       "CapsLock", "NumLock", "ScrollLock", "LMB", "RMB", "MMB"]
)


def test_every_key_name_has_a_cap():
    ids = {k.id for k in keys.keycaps()}
    assert set(SDK_KEY_NAMES) <= ids
    assert len(ids) == len(keys.keycaps())


def test_every_entry_draws():
    catalog = {k.id: k for k in keys.everything()}
    for k in catalog.values():
        svg = svgkit.key_svg(k, catalog)
        assert svg.startswith("<svg") and "<path" in svg


def test_layout_fits_without_touching_the_glyphs_the_game_uses():
    info = pack.load_info()
    entries = keys.everything()
    placed, prot = pack.layout(info, entries, 22, attempts=0)
    forbidden = pack.FORBIDDEN
    assert len({p["char"] for p in placed}) == len(placed)
    assert all(p["char"] not in prot and p["char"] not in forbidden for p in placed)
    occ = pack.Occupancy(info, prot)
    for p in placed:
        assert 0 <= p["u"] and p["u"] + p["w"] <= pack.PAGE and p["v"] + p["h"] <= pack.PAGE
        region = occ.occ[p["page"]][p["v"]:p["v"] + p["h"], p["u"]:p["u"] + p["w"]]
        assert not region.any(), p["id"]  # no overlap with protected cells or earlier caps
        occ.take(p["page"], p["u"], p["v"], p["w"], p["h"])


def test_committed_include_matches_layout_id():
    info = pack.load_info()
    placed, _ = pack.layout(info, keys.everything(), 22, attempts=0)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    text = open(os.path.join(root, "src", "gow2_keycap_layout.inc"), encoding="utf-8").read()
    assert re.search(r'kLayoutId = "([0-9a-f]{16})"', text).group(1) == pack.layout_id(placed, 22)
