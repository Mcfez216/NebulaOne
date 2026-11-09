#!/usr/bin/env python3
"""Generate the NebulaOne logo set (SVG + PNG + ICO).

The mark is a sharp "N" whose right stroke rises into a "1", cut through by a
tilted orbit ring with a single bright star. The wordmark is Chakra Petch
SemiBold converted to outlines, so the SVGs render the same on every machine.
Colours: ivory letters with a signal-teal orbit on deep space.

Usage:  python3 tools/make_logo.py            (run from the repo root)
Needs:  fonttools (pip), rsvg-convert, ImageMagick `convert` for the .ico
"""
import math
import os
import subprocess

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "logo")
FONT = os.path.join(ROOT, "assets", "fonts", "ChakraPetch-SemiBold.ttf")

CYAN = "#46C4B2"  # signal teal, the default livery
INK_TOP = "#F3ECDB"
INK_BOTTOM = "#B9AE96"
BG = "#0B0D12"

# ---------------------------------------------------------------- the mark --
# All geometry lives in a 512 x 512 box, centred on (256, 256).
DY = 23  # shift so the letters sit optically centred
LEFT_STEM = [(150, 362), (150, 150), (190, 150), (190, 362)]
DIAGONAL = [(150, 150), (196, 150), (362, 362), (316, 362)]
ONE = [(362, 362), (362, 104), (318, 104), (282, 138), (282, 162), (322, 130), (322, 362)]

RX, RY, TILT = 205, 62, -16  # orbit ellipse


def poly(points):
    return "M" + " L".join(f"{x},{y + DY}" for x, y in points) + " Z"


def orbit_point(t_deg):
    t = math.radians(t_deg)
    x, y = RX * math.cos(t), RY * math.sin(t)
    a = math.radians(TILT)
    return 256 + x * math.cos(a) - y * math.sin(a), 256 + x * math.sin(a) + y * math.cos(a)


def mark_group(uid):
    """SVG fragment for the mark; uid keeps gradient/mask ids unique per file."""
    back = f"M{-RX},0 A{RX},{RY} 0 0 1 {RX},0"
    front = f"M{RX},0 A{RX},{RY} 0 0 1 {-RX},0"
    ring_tf = f"translate(256,256) rotate({TILT})"
    sx, sy = orbit_point(58)
    letters = poly(LEFT_STEM) + " " + poly(DIAGONAL) + " " + poly(ONE)
    return f"""
  <defs>
    <linearGradient id="ink{uid}" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{INK_TOP}"/>
      <stop offset="1" stop-color="{INK_BOTTOM}"/>
    </linearGradient>
    <radialGradient id="star{uid}">
      <stop offset="0" stop-color="#FFFFFF"/>
      <stop offset="0.35" stop-color="#FFE2B0"/>
      <stop offset="1" stop-color="{CYAN}" stop-opacity="0"/>
    </radialGradient>
    <mask id="cut{uid}" maskUnits="userSpaceOnUse" x="0" y="0" width="512" height="512">
      <rect width="512" height="512" fill="#fff"/>
      <path d="{front}" transform="{ring_tf}" fill="none" stroke="#000" stroke-width="28"/>
    </mask>
  </defs>
  <path d="{back}" transform="{ring_tf}" fill="none" stroke="{CYAN}" stroke-opacity="0.38" stroke-width="9" stroke-linecap="round"/>
  <path d="{letters}" fill="url(#ink{uid})" mask="url(#cut{uid})"/>
  <path d="{front}" transform="{ring_tf}" fill="none" stroke="{CYAN}" stroke-width="9" stroke-linecap="round"/>
  <circle cx="{sx:.1f}" cy="{sy:.1f}" r="30" fill="url(#star{uid})"/>
  <circle cx="{sx:.1f}" cy="{sy:.1f}" r="6.5" fill="#FFF6E6"/>"""


# ------------------------------------------------------------ the wordmark --
def wordmark(text_parts, cap_px, tracking_em):
    """Return (svg_paths, width, cap_px). text_parts = [(text, colour), ...]."""
    font = TTFont(FONT)
    upm = font["head"].unitsPerEm
    cap = font["OS/2"].sCapHeight or int(upm * 0.7)
    scale = cap_px / cap
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    x = 0.0
    out = []
    for text, colour in text_parts:
        for i, ch in enumerate(text):
            gname = cmap[ord(ch)]
            pen = SVGPathPen(gs)
            # flip y (font units are y-up), baseline at y = cap_px
            tpen = TransformPen(pen, (scale, 0, 0, -scale, x, cap_px))
            gs[gname].draw(tpen)
            out.append(f'<path d="{pen.getCommands()}" fill="{colour}"/>')
            x += hmtx[gname][0] * scale + tracking_em * cap_px / 0.7
    width = x - tracking_em * cap_px / 0.7  # no tracking after the last glyph
    return "\n    ".join(out), width


def write(name, body, w, h):
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" width="{w:.0f}" height="{h:.0f}">{body}\n</svg>\n')
    return path


def png(svg, out, width=None, height=None):
    args = ["rsvg-convert", svg, "-o", out]
    if width:
        args += ["-w", str(width)]
    if height:
        args += ["-h", str(height)]
    subprocess.run(args, check=True)


def main():
    os.makedirs(OUT, exist_ok=True)

    # 1. Mark only, transparent background
    mark_svg = write("nebulaone-mark.svg", mark_group("m"), 512, 512)

    # 2. App icon: mark on a rounded dark tile
    icon_body = f"""
  <defs>
    <radialGradient id="tile" cx="0.5" cy="0.42" r="0.75">
      <stop offset="0" stop-color="#1C2433"/>
      <stop offset="1" stop-color="{BG}"/>
    </radialGradient>
  </defs>
  <rect x="8" y="8" width="496" height="496" rx="56" fill="url(#tile)" stroke="#2E3A4A" stroke-width="4"/>
  <g transform="translate(256,256) scale(0.86) translate(-256,-256)">{mark_group("i")}
  </g>"""
    icon_svg = write("nebulaone-icon.svg", icon_body, 512, 512)

    # 3. Horizontal logo: mark + wordmark
    paths, ww = wordmark([("NEBULA", INK_TOP), ("ONE", CYAN)], cap_px=92, tracking_em=0.10)
    gap = 36
    W = 512 + gap + ww + 24
    horiz = f"""
  <g>{mark_group("h")}
  </g>
  <g transform="translate({512 + gap},{256 - 46})">
    {paths}
  </g>"""
    horiz_svg = write("nebulaone-logo.svg", horiz, W, 512)

    # 4. Stacked logo for the title screen: mark above wordmark, plus a strapline rule
    paths2, ww2 = wordmark([("NEBULA", INK_TOP), ("ONE", CYAN)], cap_px=74, tracking_em=0.14)
    W2 = max(ww2 + 80, 760)
    H2 = 512 + 150
    stacked = f"""
  <g transform="translate({(W2 - 512) / 2},0)">{mark_group("s")}
  </g>
  <g transform="translate({(W2 - ww2) / 2},{512 + 6})">
    {paths2}
  </g>"""
    stacked_svg = write("nebulaone-logo-stacked.svg", stacked, W2, H2)

    # PNG renders
    png(mark_svg, os.path.join(OUT, "nebulaone-mark-256.png"), 256, 256)
    png(mark_svg, os.path.join(OUT, "nebulaone-mark-64.png"), 64, 64)
    png(horiz_svg, os.path.join(OUT, "nebulaone-logo-1600.png"), width=1600)
    png(stacked_svg, os.path.join(OUT, "nebulaone-logo-stacked-900.png"), width=900)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    ico_parts = []
    for s in sizes:
        p = os.path.join(OUT, f"icon-{s}.png")
        png(icon_svg, p, s, s)
        ico_parts.append(p)
    subprocess.run(["convert", *ico_parts, os.path.join(OUT, "nebulaone.ico")], check=True)
    print("logo set written to", OUT)


if __name__ == "__main__":
    main()
