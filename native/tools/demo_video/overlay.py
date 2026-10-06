"""The input overlay: key caps, a scrolling per-key timeline and a "moving + camera" badge, drawn on a 1280x720 frame.

draw(img, f, keys, all_frames, title) draws on a PIL image in place. `keys` is the set held on script frame f;
`all_frames` is the held set of every script frame (for the timeline and for which key caps to show).
"""
from PIL import ImageDraw, ImageFont

FONT_DIR = "/System/Library/Fonts/Supplemental/"
_BOLD, _REG = FONT_DIR + "Arial Bold.ttf", FONT_DIR + "Arial.ttf"
GOLD, DIM, INK, CAM = (255, 205, 40), (140, 140, 150), (20, 20, 20), (110, 210, 255)
BOXES = [({"Space"}, "Space", "JUMP", GOLD), ({"LeftShift"}, "Shift", "POUND / CROUCH", GOLD), ({"R"}, "R", "THROW CAPPY", GOLD),
         ({"F"}, "F", "SPIN", GOLD), ({"J", "L"}, "J / L", "ORBIT CAMERA", CAM), ({"I", "K"}, "I / K", "TILT CAMERA", CAM),
         ({"Z", "X"}, "Z / X", "ZOOM", CAM)]
ROWS = [("MOVE", {"W", "A", "S", "D"}, GOLD), ("JUMP", {"Space"}, GOLD), ("POUND", {"LeftShift"}, GOLD), ("THROW", {"R"}, GOLD),
        ("SPIN", {"F"}, GOLD), ("ORBIT", {"J", "L"}, CAM), ("TILT", {"I", "K"}, CAM), ("ZOOM", {"Z", "X"}, CAM)]
MOVES, CAMS = {"W", "A", "S", "D", "Space", "LeftShift", "R", "F"}, {"J", "L", "I", "K", "Z", "X"}
PAST, FUTURE = 150, 60  # timeline window, in frames


def _fonts():
    try:
        return (ImageFont.truetype(_BOLD, 19), ImageFont.truetype(_REG, 14), ImageFont.truetype(_BOLD, 22), ImageFont.truetype(_BOLD, 10))
    except OSError:  # no macOS fonts: PIL's built-in bitmap font keeps the tool usable (and testable) elsewhere
        f = ImageFont.load_default()
        return f, f, f, f


bold, small, cap, tiny = _fonts()


def fit_font(d, text, width, fonts):
    """The first font (largest first) in which `text` is at most `width` pixels wide; the last one if none is."""
    for font in fonts:
        if d.textlength(text, font=font) <= width:
            return font
    return fonts[-1]


def _keycap(d, x, y, w, h, label, lit):
    d.rounded_rectangle([x, y, x + w, y + h], radius=7, fill=GOLD if lit else (0, 0, 0, 0), outline=GOLD if lit else DIM, width=3)
    tw = d.textlength(label, font=cap)
    d.text((x + (w - tw) / 2, y + h / 2 - 13), label, font=cap, fill=INK if lit else DIM)


def used_keys(all_frames):
    return {k for s in all_frames for k in s}


def _rows(used):
    return [r for r in ROWS if r[1] & used or r[0] in ("MOVE", "JUMP")]


def reserved_height(used):
    """Pixels the overlay needs at the bottom of a 1280x720 frame: the key strip, the timeline and the badge row."""
    return 100 + 12 * len(_rows(used)) + 14 + 44


def draw(img, f, keys, all_frames, title, used=None, band=False):
    """Draw on a 1280x720 PIL image. band=True: the overlay sits in its own band (the game image is above it, see
    reserved_height) and the title and frame counter move into the band; False: it is laid over the game image."""
    d = ImageDraw.Draw(img, "RGBA")
    used = used if used is not None else used_keys(all_frames)
    top = 720 - 100
    d.rectangle([0, top, 1280, 720], fill=(0, 0, 0, 255 if band else 170))
    k, x0, y0 = 40, 16, top + 8
    for label, dx, dy in (("W", 1, 0), ("A", 0, 1), ("S", 1, 1), ("D", 2, 1)):
        _keycap(d, x0 + dx * (k + 4), y0 + dy * (k + 4), k, k, label, label in keys)
    boxes = [b for b in BOXES if b[0] & {"Space", "LeftShift", "R"} or b[0] & used]  # jump/pound/throw always; the rest when used
    x, wbox = 150, (1280 - 150 - 10) // len(boxes) - 8
    # One font per kind of label for every box (the largest in which the widest label fits), so the strip looks even.
    widest_name = max((b[2] for b in boxes), key=lambda t: d.textlength(t, font=bold))
    widest_key = max((b[1] for b in boxes), key=lambda t: d.textlength(t, font=cap))
    name_font = fit_font(d, widest_name, wbox - 20, (bold, small, tiny))
    key_font = fit_font(d, widest_key, wbox - 20, (cap, bold, small))
    for keyset, label, name, colour in boxes:
        lit = bool(keyset & keys)
        d.rounded_rectangle([x, top + 12, x + wbox, top + 88], radius=10, fill=colour if lit else (0, 0, 0, 0),
                            outline=colour if lit else DIM, width=3)
        d.text((x + 10, top + 18), label, font=key_font, fill=INK if lit else DIM)
        d.text((x + 10, top + 54), name, font=name_font, fill=INK if lit else DIM)
        x += wbox + 8
    # timeline: PAST frames back, FUTURE ahead (dimmed), one row per input in use
    rows = _rows(used)
    h, tl_top, left, right = 12, top - 12 * len(rows) - 14, 70, 1270
    d.rectangle([0, tl_top - 6, 1280, tl_top + len(rows) * h + 6], fill=(0, 0, 0, 255 if band else 150))
    if band:
        d.rectangle([0, tl_top - 44, 1280, tl_top - 6], fill=(0, 0, 0, 255))
    xs = lambda g: left + (g - (f - PAST)) * (right - left) / (PAST + FUTURE)
    last = min(len(all_frames), f + FUTURE)
    for i, (name, keyset, colour) in enumerate(rows):
        y = tl_top + i * h
        d.text((8, y - 2), name, font=tiny, fill=colour)
        g0 = None
        for g in range(max(0, f - PAST), last + 1):
            on = g < len(all_frames) and bool(all_frames[g] & keyset)
            if on and g0 is None:
                g0 = g
            if (not on or g == last) and g0 is not None:
                d.rectangle([xs(g0), y + 1, xs(g), y + h - 2], fill=colour if g0 <= f else tuple(c // 2 for c in colour))
                g0 = None
    d.line([xs(f), tl_top - 6, xs(f), tl_top + len(rows) * h + 6], fill=(255, 255, 255), width=2)
    both = bool(keys & MOVES) and bool(keys & CAMS)
    label = "MOVING + CAMERA AT THE SAME TIME"
    tw = d.textlength(label, font=bold)
    d.rounded_rectangle([1280 - tw - 34, tl_top - 40, 1270, tl_top - 10], radius=8, fill=(255, 255, 255) if both else (0, 0, 0, 120),
                        outline=(255, 255, 255) if both else DIM, width=2)
    d.text((1280 - tw - 22, tl_top - 37), label, font=bold, fill=INK if both else DIM)
    ty = tl_top - 34 if band else 8  # band layout: the labels live in the band, off the scene
    d.text((12, ty), (title + "  -  " if title else "") + "keyboard input as held this frame", font=small, fill=(255, 255, 255))
    d.text((12 if band else 1180, ty + 16 if band else 8), f"frame {f:3d}", font=small, fill=(255, 255, 255))
