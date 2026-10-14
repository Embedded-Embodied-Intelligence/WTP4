#!/usr/bin/env python3
"""Generuje wykresy SVG do slajdów z danych zmierzonych w Wokwi (bez dodatkowych bibliotek).

Dane:
  - predykcja: scenariusz T4 z TESTY.md (prognoza kontroli w funkcji temperatury, wersja 0.4.0),
  - FPU: monitor integralności FPU z linii [DIAG] (DOKUMENTACJA.md, tab. 7.3).

Uruchomienie (z katalogu prezentacja/):  python3 wykresy.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent / "img"
FONT = "Helvetica Neue, Helvetica, Arial, sans-serif"
INK, GRID, MUTED = "#1F2933", "#D9E2EC", "#52606D"
TEAL, AMBER, RED, GREEN = "#087E8B", "#E0A100", "#C62828", "#2E7D32"

# Scenariusz T4: (temperatura [°C], prognoza [h]) z linii stanu; zdarzenie STAN -> PLAN przy ok. 52 °C (47 h).
PREDYKCJA = [(45, 75), (47, 68), (48, 62), (49, 58), (50, 53), (51, 50), (53, 44), (55, 41), (56, 38), (57, 35), (58, 32)]
PLAN_EVENT = (52, 47)

# Skumulowane błędy FPU [czas s, błędy]: wersja 0.3.0 (akwizycja na float) i 0.4.0 (tylko liczby całkowite).
FPU_V03 = [(0, 0), (30, 2), (60, 7), (90, 21), (120, 34), (150, 48), (180, 63), (210, 79), (240, 95), (270, 105)]
FPU_V04 = [(0, 0)] + [(t, 0) for t in range(30, 391, 30)]

W, H = 1200, 620
L, R, T, B = 110, 40, 40, 90  # marginesy obszaru wykresu


class Chart:
    def __init__(self, x0, x1, y0, y1):
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.parts = []

    def px(self, x):
        return L + (x - self.x0) / (self.x1 - self.x0) * (W - L - R)

    def py(self, y):
        return H - B - (y - self.y0) / (self.y1 - self.y0) * (H - T - B)

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, size=22, color=INK, anchor="start", weight="normal"):
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{FONT}" font-size="{size}" fill="{color}" '
                 f'text-anchor="{anchor}" font-weight="{weight}">{s}</text>')

    def axes(self, xticks, yticks, xlabel, ylabel):
        for v in yticks:
            y = self.py(v)
            self.add(f'<line x1="{L}" y1="{y:.1f}" x2="{W - R}" y2="{y:.1f}" stroke="{GRID}" stroke-width="1.5"/>')
            self.text(L - 14, y + 7, str(v), 20, MUTED, "end")
        for v in xticks:
            x = self.px(v)
            self.add(f'<line x1="{x:.1f}" y1="{H - B}" x2="{x:.1f}" y2="{H - B + 8}" stroke="{MUTED}" stroke-width="2"/>')
            self.text(x, H - B + 34, str(v), 20, MUTED, "middle")
        self.add(f'<line x1="{L}" y1="{H - B}" x2="{W - R}" y2="{H - B}" stroke="{MUTED}" stroke-width="2"/>')
        self.text((L + W - R) / 2, H - 18, xlabel, 22, INK, "middle")
        self.add(f'<text x="28" y="{(T + H - B) / 2:.1f}" font-family="{FONT}" font-size="22" fill="{INK}" '
                 f'text-anchor="middle" transform="rotate(-90 28 {(T + H - B) / 2:.1f})">{ylabel}</text>')

    def polyline(self, pts, color, width=5, dash=None, dots=True):
        d = " ".join(f"{self.px(x):.1f},{self.py(y):.1f}" for x, y in pts)
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
                 f'stroke-linejoin="round" stroke-linecap="round"{extra}/>')
        if dots:
            for x, y in pts:
                self.add(f'<circle cx="{self.px(x):.1f}" cy="{self.py(y):.1f}" r="6" fill="{color}"/>')

    def svg(self):
        body = "\n  ".join(self.parts)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">\n'
                f'  <rect width="{W}" height="{H}" fill="white"/>\n  {body}\n</svg>\n')


def wykres_predykcja():
    c = Chart(44, 61, 0, 80)
    c.axes(range(44, 62, 2), range(0, 81, 20), "temperatura obudowy [°C] (rośnie o 1 °C na godzinę pracy)",
           "prognoza kontroli [h]")
    # pas wyprzedzenia: od zdarzenia PLAN do progu ostrzeżenia
    x1, x2 = c.px(PLAN_EVENT[0]), c.px(60)
    c.add(f'<rect x="{x1:.1f}" y="{T}" width="{x2 - x1:.1f}" height="{H - T - B}" fill="{TEAL}" opacity="0.08"/>')
    c.text((x1 + x2) / 2, T + 34, "8 °C wyprzedzenia", 24, TEAL, "middle", "bold")
    # progi
    y48 = c.py(48)
    c.add(f'<line x1="{L}" y1="{y48:.1f}" x2="{W - R}" y2="{y48:.1f}" stroke="{AMBER}" stroke-width="3" stroke-dasharray="12 8"/>')
    c.text(x2 - 12, y48 - 12, "próg PLAN: prognoza ≤ 48 h", 21, AMBER, "end", "bold")
    c.add(f'<line x1="{x2:.1f}" y1="{T}" x2="{x2:.1f}" y2="{H - B}" stroke="{RED}" stroke-width="3" stroke-dasharray="12 8"/>')
    c.text(x2 - 8, H - B - 16, "próg ostrzeżenia 60 °C", 21, RED, "end", "bold")
    # dane
    c.polyline(PREDYKCJA, TEAL)
    ex, ey = c.px(PLAN_EVENT[0]), c.py(PLAN_EVENT[1])
    c.add(f'<circle cx="{ex:.1f}" cy="{ey:.1f}" r="13" fill="none" stroke="{AMBER}" stroke-width="5"/>')
    c.text(ex - 18, ey + 46, "STAN → PLAN (52 °C, 47 h)", 22, INK, "end", "bold")
    c.text(c.px(45) + 12, c.py(75) - 18, "prognoza wg trendu temperatury", 21, TEAL, "start")
    return c.svg()


def wykres_fpu():
    c = Chart(0, 400, 0, 120)
    c.axes(range(0, 401, 50), range(0, 121, 20), "czas pracy symulacji [s]", "skumulowane błędy FPU")
    c.polyline(FPU_V03, RED)
    c.polyline(FPU_V04, GREEN)
    c.text(c.px(270) - 10, c.py(105) - 20, "0.3.0: akwizycja na float – 105 błędów", 23, RED, "end", "bold")
    c.text(c.px(390), c.py(0) - 22, "0.4.0: akwizycja na liczbach całkowitych – 0 / 34 658", 23, GREEN, "end", "bold")
    return c.svg()


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    (OUT / "wykres_predykcja.svg").write_text(wykres_predykcja(), encoding="utf-8")
    (OUT / "wykres_fpu.svg").write_text(wykres_fpu(), encoding="utf-8")
    print("zapisano:", ", ".join(p.name for p in sorted(OUT.glob("wykres_*.svg"))))
