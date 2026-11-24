#!/usr/bin/env python3
"""Render the six real data/demo/ ICU records to dependency-free SVG plots.

Stdlib only. Runs build/qw on each record (rules/alarm_rules.json +
config/detector.json), parses the .qw binary directly, and draws the trailing
16 s before the alarm (alarm at 300 s) as polylines. Writes:

  docs/plots/<record>.svg      one crisp plot per record
  docs/plots/demo_overview.svg 3x2 grid of all six

Data: real PhysioNet/CinC 2015 train-split records (ODC-BY 1.0,
DOI 10.13026/c9fg-a467). Research demo, not a medical device.
"""
import json
import struct
import subprocess
import sys
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build/qw"
RULES = "rules/alarm_rules.json"
CONFIG = "config/detector.json"
OUT = ROOT / "docs/plots"

WINDOW_S = 16
ALARM_S = 300.0
TRAIL_S = 5.0
BUCKETS = 200  # min/max pairs -> ~400 polyline points per trace

BG, PANEL, GRID = "#0f172a", "#1e293b", "#334155"
TXT, MUTED = "#e2e8f0", "#94a3b8"
ECG_COL, PULSE_COL = "#38bdf8", "#f472b6"
AMBER, GREEN, SLATE = "#f59e0b", "#22d3ee", "#64748b"
FONT = "ui-monospace, Menlo, Consolas, monospace"
FOOTER = ("Real PhysioNet/CinC 2015 train-split ICU record (ODC-BY 1.0, "
          "DOI 10.13026/c9fg-a467). Research demo, not a medical device; "
          "rules pending_signoff.")

ECG_PREF = ["II", "V", "I", "ECG"]
PULSE_PREF = ["ABP", "PLETH", "PPG"]


def read_qw(path):
    d = Path(path).read_bytes()
    if d[:4] != b"QW01":
        raise ValueError(f"{path}: bad magic")
    fs, nch, ns = struct.unpack("<III", d[4:16])
    names = [d[16 + 16 * i:32 + 16 * i].split(b"\0")[0].decode("ascii") for i in range(nch)]
    off = 16 + 16 * nch
    if len(d) != off + 4 * nch * ns:
        raise ValueError(f"{path}: size mismatch")
    chans = {}
    for i, nm in enumerate(names):  # channel-major
        chans[nm] = struct.unpack(f"<{ns}f", d[off + 4 * i * ns: off + 4 * (i + 1) * ns])
    return fs, ns, names, chans


def span(v):
    return max(v) - min(v) if v else 0.0


def pick(names, chans, prefs, win):
    cands = [n for p in prefs for n in names if n == p]
    for n in cands:
        if span(chans[n][win[0]:win[1]]) > 0:
            return n
    for n in names:  # fallback: any non-flat channel not already ECG-like
        if n not in ECG_PREF and span(chans[n][win[0]:win[1]]) > 0:
            return n
    return cands[0] if cands else names[0]


def envelope(v, w, h, x0, y0, buckets=BUCKETS):
    """Min/max envelope -> polyline points normalized into a w x h box at (x0, y0)."""
    n = len(v)
    lo, hi = min(v), max(v)
    rng = hi - lo
    pts = []
    for b in range(buckets):
        seg = v[b * n // buckets:(b + 1) * n // buckets]
        if not seg:
            continue
        a, z = min(seg), max(seg)
        xs = x0 + w * (b + 0.5) / buckets
        if seg.index(a) > seg.index(z):
            a, z = z, a
        for val in (a, z):
            y = y0 + h / 2 if rng == 0 else y0 + h - (val - lo) / rng * h
            pts.append(f"{xs:.1f},{y:.1f}")
    return " ".join(pts)


def run_qw(rec):
    p = subprocess.run([str(BIN), f"data/demo/{rec['file']}", rec["alarm_type"], RULES, CONFIG],
                       capture_output=True, text=True, cwd=ROOT, timeout=60)
    if p.returncode != 0:
        raise RuntimeError(f"qw failed on {rec['record']}: {p.stderr.strip()}")
    r = json.loads(p.stdout)
    if r.get("rules_status") != "pending_signoff":
        raise RuntimeError(f"{rec['record']}: rules_status is {r.get('rules_status')!r}, expected pending_signoff")
    return r


def E(parent, tag, text=None, **attrs):
    el = ET.SubElement(parent, tag, {k.replace("_", "-"): str(v) for k, v in attrs.items()})
    if text is not None:
        el.text = text
    return el


def badge_colors(decision):
    return (GREEN, "#042f2e") if decision == "SUPPRESS_TO_REVIEW" else (AMBER, "#1c1203")


def load_record(rec):
    fs, ns, names, chans = read_qw(ROOT / "data/demo" / rec["file"])
    alarm = min(int(ALARM_S * fs), ns)
    start = max(0, alarm - int(WINDOW_S * fs))
    win = (start, alarm)
    ecg = pick(names, chans, ECG_PREF, win)
    pulse = pick([n for n in names if n != ecg], chans, PULSE_PREF, win)
    return {"fs": fs, "start": start, "alarm": alarm, "ecg": ecg, "pulse": pulse,
            "ecg_v": chans[ecg][start:alarm], "pulse_v": chans[pulse][start:alarm]}


def trace(svg, x, y, w, h, label, values, color, sqi, highlight):
    E(svg, "rect", x=x, y=y, width=w, height=h, rx=6, fill=PANEL)
    pl, pt, pr, pb = 12, 22, 12, 8
    ix, iy, iw, ih = x + pl, y + pt, w - pl - pr, h - pt - pb
    for k in range(1, 4):  # 4 s gridlines
        gx = ix + iw * k / 4
        E(svg, "line", x1=f"{gx:.1f}", y1=iy, x2=f"{gx:.1f}", y2=iy + ih, stroke=GRID, stroke_width=1)
    if highlight:
        bw = iw * TRAIL_S / WINDOW_S
        E(svg, "rect", x=f"{ix + iw - bw:.1f}", y=iy, width=f"{bw:.1f}", height=ih,
          fill=AMBER, fill_opacity="0.16")
    E(svg, "polyline", points=envelope(values, iw, ih - 6, ix, iy + 3), fill="none",
      stroke=color, stroke_width=1.2, stroke_linejoin="round")
    E(svg, "text", f"{label}  sqi={sqi:.2f}", x=x + pl, y=y + 15, fill=MUTED,
      font_family=FONT, font_size=12)


def wrap_reason(reason, width):
    return textwrap.wrap(reason, width=width, break_long_words=False)[:5]


def record_group(svg, rec, res, data, x, y, w, h, compact):
    d = res["decision"]
    truth = "true alarm" if rec["true_alarm"] else "false alarm"
    badge_fill, badge_txt = badge_colors(d)
    title_size = 15 if compact else 20
    head = f"{rec['record']} · {rec['alarm_type']} · {truth}"
    if not compact:
        head += f" -> {d}"
    E(svg, "text", head,
      x=x + 14, y=y + (24 if compact else 34), fill=TXT, font_family=FONT,
      font_size=title_size, font_weight="bold")
    bw = 190 if compact else 230
    E(svg, "rect", x=x + w - bw - 14, y=y + (8 if compact else 14), width=bw, height=24 if compact else 28,
      rx=12, fill=badge_fill)
    E(svg, "text", d, x=x + w - bw / 2 - 14, y=y + (25 if compact else 33), fill=badge_txt,
      font_family=FONT, font_size=12 if compact else 14, font_weight="bold", text_anchor="middle")
    top = y + (42 if compact else 56)
    avail = h - (42 if compact else 56)
    tr_h = (avail - (96 if compact else 120)) / 2
    hl = rec["alarm_type"] == "Asystole"
    trace(svg, x + 10, top, w - 20, tr_h, f"ECG {data['ecg']}", data["ecg_v"], ECG_COL, res["ecg_sqi"], hl)
    trace(svg, x + 10, top + tr_h + 6, w - 20, tr_h, f"pulse {data['pulse']}", data["pulse_v"],
          PULSE_COL, res["pulse_sqi"], hl)
    ty = top + 2 * tr_h + 12
    E(svg, "text", "-16 s" + " " * 3 + "(alarm at 300 s)" + (" " * 3 + "amber band: trailing 5 s" if hl else ""),
      x=x + 14, y=ty + 10, fill=MUTED, font_family=FONT, font_size=11)
    cw = 62 if compact else 110
    fs_ = 11 if compact else 13
    for i, line in enumerate(wrap_reason(res["reason"], cw)):
        E(svg, "text", line, x=x + 14, y=ty + 30 + i * (fs_ + 4), fill=TXT,
          font_family=FONT, font_size=fs_)


def new_svg(w, h):
    svg = ET.Element("svg", {"xmlns": "http://www.w3.org/2000/svg", "width": str(w), "height": str(h),
                             "viewBox": f"0 0 {w} {h}", "role": "img"})
    E(svg, "rect", x=0, y=0, width=w, height=h, fill=BG)
    return svg


def footer(svg, x, y, size):
    for i, line in enumerate(textwrap.wrap(FOOTER, width=int(1500 / (size * 0.62)) if size > 12 else 150)):
        E(svg, "text", line, x=x, y=y + i * (size + 4), fill=MUTED, font_family=FONT, font_size=size)


def write(svg, path, title):
    ET.SubElement(svg, "title").text = title
    ET.indent(svg)
    tree = ET.ElementTree(svg)
    tree.write(path, encoding="utf-8", xml_declaration=True)
    ET.parse(path)  # validate


def main():
    if not BIN.exists():
        print("missing build/qw; run `make` first", file=sys.stderr)
        return 2
    playlist = json.loads((ROOT / "data/demo/PLAYLIST.json").read_text())["playlist"]
    manifest = {r["record"]: r for r in json.loads((ROOT / "data/demo/MANIFEST.json").read_text())["records"]}
    OUT.mkdir(parents=True, exist_ok=True)
    items = []
    for name in playlist:
        rec = manifest[name]
        items.append((rec, run_qw(rec), load_record(rec)))

    W, H = 1200, 640
    for rec, res, data in items:
        svg = new_svg(W, H)
        record_group(svg, rec, res, data, 0, 0, W, H - 60, compact=False)
        footer(svg, 20, H - 40, 12)
        write(svg, OUT / f"{rec['record']}.svg", f"{rec['record']} {rec['alarm_type']} {res['decision']}")

    cw, ch, gap, top = 640, 400, 12, 70
    OW, OH = 3 * cw + 4 * gap, top + 2 * (ch + gap) + 50
    svg = new_svg(OW, OH)
    E(svg, "text", "Quiet Ward 2.0: six real ICU records, trailing 16 s before each alarm", x=gap + 8, y=34,
      fill=TXT, font_family=FONT, font_size=22, font_weight="bold")
    E(svg, "text", "build/qw decisions; t116s is a false alarm that is still confirmed (conservative miss)",
      x=gap + 8, y=56, fill=MUTED, font_family=FONT, font_size=13)
    for i, (rec, res, data) in enumerate(items):
        cx, cy = gap + (i % 3) * (cw + gap), top + (i // 3) * (ch + gap)
        stroke = GREEN if res["decision"] == "SUPPRESS_TO_REVIEW" else (AMBER if rec["record"] == "t116s" else SLATE)
        E(svg, "rect", x=cx, y=cy, width=cw, height=ch, rx=8, fill=BG, stroke=stroke,
          stroke_width=2.5 if rec["record"] in ("a109l", "t116s") else 1)
        record_group(svg, rec, res, data, cx, cy, cw, ch, compact=True)
    footer(svg, gap + 8, OH - 36, 11)
    write(svg, OUT / "demo_overview.svg", "Quiet Ward 2.0 real ICU demo records")

    for p in sorted(OUT.glob("*.svg")):
        ET.parse(p)
        print(f"ok {p.relative_to(ROOT)} ({p.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
