"""Draw the prototype wiring as an editable SVG (not a PCB layout)."""
from pathlib import Path
from xml.sax.saxutils import escape

OUT = Path(__file__).with_name("SCHEMA_ESP32S3_TB6612FNG.svg")
parts = []


def add(s):
    parts.append(s)


def rect(x, y, w, h, fill, radius=18, stroke="#526374", sw=3):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')


def txt(x, y, value, size=26, color="#1b2d40", weight=600, anchor="start"):
    add(f'<text x="{x}" y="{y}" font-family="DejaVu Sans, Arial, sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" fill="{color}">{escape(value)}</text>')


def line(points, color, width=8, dash=""):
    pts = " ".join(f"{x},{y}" for x, y in points)
    add(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"' + (f' stroke-dasharray="{dash}"' if dash else '') + '/>')


def dot(x, y, color, r=8):
    add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}" stroke="#f7f9fa" stroke-width="3"/>')


def pin(x, y, label, side="left", color="#2b536d", size=24):
    dot(x, y, color, 10)
    tx = x + 23 if side == "left" else x - 23
    txt(tx, y + 8, label, size, "#ffffff", 650, "start" if side == "left" else "end")


add('<svg xmlns="http://www.w3.org/2000/svg" width="2600" height="1850" viewBox="0 0 2600 1850">')
rect(0, 0, 2600, 1850, "#f6f8fa", 0, "none", 0)
rect(0, 0, 2600, 112, "#183a55", 0, "none", 0)
txt(75, 72, "ESP32-S3 • TB6612FNG • 2 мотори", 47, "#ffffff", 700)
txt(2440, 70, "СХЕМА ПАЙКИ", 29, "#e1ebf2", 600, "end")
txt(75, 157, "Живлення 2S → 5 В для ESP/серво та 12 В для моторів. Усі мінуси об’єднані.", 28)

# Power route, drawn behind the module pictures.
RED = "#c9343a"; BLACK = "#263747"; ORANGE = "#e59322"; MAGENTA = "#a4487b"
BLUE = "#1877b8"; CYAN = "#1d9ab8"; GREEN = "#2e9f6a"; PURPLE = "#8057aa"
line([(1110, 195), (1110, 180), (1535, 180), (1535, 260), (1565, 260)], RED, 10)
line([(1755, 260), (1790, 260), (1790, 365), (270, 365), (270, 515), (310, 515)], RED, 10)
line([(1790, 365), (1760, 365), (1760, 515), (1810, 515)], RED, 10)
line([(1250, 275), (1250, 315), (125, 315), (125, 1710), (2450, 1710)], BLACK, 10)
line([(125, 565), (310, 565)], BLACK, 9)
line([(1810, 565), (1770, 565), (1770, 1710)], BLACK, 9)

# Battery and switch.
rect(1040, 195, 350, 120, "#394c62", 20)
rect(1070, 216, 146, 75, "#617ba0", 16, "none", 0)
rect(1210, 216, 146, 75, "#617ba0", 16, "none", 0)
txt(1215, 249, "2S АКУМУЛЯТОР", 25, "#ffffff", 700, "middle")
txt(1215, 284, "7,4 В • до 8,4 В", 21, "#f0f4f8", 500, "middle")
dot(1110, 195, RED); dot(1250, 315, BLACK)
rect(1565, 195, 190, 130, "#293541", 18)
rect(1630, 212, 60, 82, "#e5e9ec", 12, "none", 0)
txt(1660, 255, "I/O", 22, "#24394a", 700, "middle")
txt(1660, 310, "ВИМИКАЧ +", 21, "#ffffff", 700, "middle")
txt(107, 302, "СПІЛЬНИЙ GND", 22, BLACK, 700)

# Converter modules, inspired by physical blue and green PCBs.
rect(310, 400, 490, 235, "#159275", 16, "#0d5f51", 5)
rect(410, 470, 130, 95, "#526477", 8, "#283c4a", 4)
txt(475, 528, "470", 37, "#e6e7e8", 700, "middle")
rect(565, 458, 67, 125, "#1e303c", 8)
rect(673, 475, 65, 110, "#1455a0", 9)
txt(555, 438, "Mini 560 Pro • ПОНИЖУВАЧ 5 В", 25, "#ffffff", 700, "middle")
pin(310, 515, "IN+", "left", RED)
pin(310, 565, "IN−", "left", BLACK)
pin(800, 510, "OUT+ 5 В", "right", ORANGE)
pin(800, 568, "OUT−", "right", BLACK)

rect(1810, 400, 490, 235, "#1869a9", 16, "#184c77", 5)
rect(1940, 465, 135, 125, "#273e56", 15)
txt(2006, 540, "470", 40, "#f3f6f7", 700, "middle")
rect(2145, 475, 72, 98, "#278ad0", 9)
txt(2055, 438, "ПІДВИЩУВАЧ • 12 В", 28, "#ffffff", 700, "middle")
pin(1810, 515, "IN+", "left", RED)
pin(1810, 565, "IN−", "left", BLACK)
pin(2300, 510, "OUT+ 12 В", "right", MAGENTA)
pin(2300, 568, "OUT−", "right", BLACK)

# 5 V and 12 V feeds from the converter outputs.
line([(800, 510), (1140, 510), (1140, 1055), (1240, 1055)], ORANGE, 8)
line([(1140, 1055), (1140, 1380), (1080, 1380), (1080, 1490), (1110, 1490)], ORANGE, 8)
line([(800, 568), (1020, 568), (1020, 1710)], BLACK, 8)
line([(2300, 510), (2380, 510), (2380, 640), (160, 640), (160, 745), (230, 745)], MAGENTA, 8)
line([(2300, 568), (2345, 568), (2345, 1710)], BLACK, 8)

# Central ESP32, intentionally illustrated as a DevKitC board, not the earlier assembled board.
rect(1240, 680, 700, 650, "#1c2835", 20, "#0a1622", 6)
rect(1450, 740, 290, 296, "#8d99a4", 22, "#ced6db", 5)
rect(1467, 765, 256, 240, "#c9d1d6", 14, "#4a5964", 3)
txt(1595, 845, "ESP32-S3", 38, "#243442", 700, "middle")
txt(1595, 892, "N16R8 • IPEX", 23, "#344655", 600, "middle")
rect(1500, 1180, 180, 118, "#d0d5d9", 13, "#6b7780", 5)
txt(1590, 1250, "USB", 35, "#3d4b57", 700, "middle")
txt(1590, 716, "КАРЕТКА: ГОЛОВНА АБО ДРУГА", 28, "#ffffff", 700, "middle")

# ESP contacts on the edges and TB6612 signal leads.
for y, lab, color in [
    (795, "42  PWM A+B", BLUE), (860, "2  IN1 A+B", CYAN),
    (925, "1  IN2 A+B", CYAN), (990, "3V3", PURPLE),
    (1055, "5V", ORANGE), (1120, "GND", BLACK),
]:
    pin(1240, y, lab, "left", color, 25)
for y, lab, color in [
    (800, "15  SERVO", GREEN), (870, "16  LED R", RED),
    (940, "17  LED G", GREEN), (1010, "8  SDA • резерв", "#8292a2"),
    (1080, "9  SCL • резерв", "#8292a2"),
    (1150, "10/11/12 • I²S резерв", "#8292a2"),
]:
    pin(1940, y, lab, "right", color, 24)

# Driver PCB: actual terminal names, grouped by function to avoid overlapping wires.
rect(230, 675, 750, 665, "#d13f42", 18, "#9f282b", 6)
rect(515, 780, 185, 300, "#27313c", 15, "#a71e27", 5)
txt(607, 865, "TB6612", 25, "#ffffff", 700, "middle")
txt(607, 915, "FNG • A+B", 19, "#dbe4eb", 600, "middle")
for xx in (295, 920):
    for yy in (715, 1300):
        add(f'<circle cx="{xx}" cy="{yy}" r="22" fill="#f6f8fa" stroke="#b72d34" stroke-width="7"/>')
txt(605, 715, "ДРАЙВЕР МОТОРІВ", 29, "#ffffff", 700, "middle")

# Driver terminal groups. Paired channel inputs are short jumpers on this module.
for y, lab, color in [
    (745, "VM 12 В", MAGENTA), (815, "GND", BLACK),
    (885, "A1 → мотор 1", "#cc8e1e"), (955, "A2 → мотор 1", "#cc8e1e"),
    (1075, "B1 → мотор 2", "#2a73ad"), (1145, "B2 → мотор 2", "#2a73ad"),
]:
    pin(230, y, lab, "left", color, 23)
for y, lab, color in [
    (795, "PWMA + PWMB", BLUE), (860, "AIN1 + BIN1", CYAN),
    (925, "AIN2 + BIN2", CYAN), (990, "VCC + STBY", PURPLE),
    (1120, "GND", BLACK),
]:
    pin(980, y, lab, "right", color, 22)

# Power and signal wires, kept in five separate horizontal lanes.
line([(230, 815), (145, 815), (145, 1710)], BLACK, 7)
line([(980, 1120), (1070, 1120), (1070, 1710)], BLACK, 7)
line([(1240, 1120), (1195, 1120), (1195, 1710)], BLACK, 7)
line([(1240, 990), (1120, 990), (980, 990)], PURPLE, 8)
line([(1240, 795), (980, 795)], BLUE, 9)
line([(1240, 860), (980, 860)], CYAN, 9)
line([(1240, 925), (980, 925)], CYAN, 9)
txt(1105, 765, "PWM", 21, BLUE, 700, "middle")
txt(1105, 837, "НАПРЯМОК", 19, CYAN, 700, "middle")
txt(1095, 979, "3,3 В", 19, PURPLE, 700, "middle")

# Simplified motor drawings, with separate output pairs.
for y, n, color in [(905, "1", "#cc8e1e"), (1100, "2", "#2a73ad")]:
    rect(24, y-76, 155, 145, "#b9c2c9", 70, "#7a8790", 5)
    add(f'<circle cx="65" cy="{y-4}" r="36" fill="#eef1f2" stroke="#79858e" stroke-width="5"/>')
    txt(112, y-5, "M", 34, "#3e4e5b", 700, "middle")
    txt(101, y+36, n, 22, "#3e4e5b", 700, "middle")
line([(179, 885), (230, 885)], "#cc8e1e", 7)
line([(179, 955), (230, 955)], "#cc8e1e", 7)
line([(179, 1075), (230, 1075)], "#2a73ad", 7)
line([(179, 1145), (230, 1145)], "#2a73ad", 7)

# Level shifter and servo (bottom row).
rect(1110, 1390, 485, 220, "#25559a", 15, "#173b6c", 5)
rect(1270, 1450, 160, 92, "#26303e", 8)
txt(1352, 1433, "TXS0108E • РІВНІ", 27, "#ffffff", 700, "middle")
pin(1110, 1490, "VB 5 В", "left", ORANGE, 22)
pin(1110, 1545, "GND", "left", BLACK, 22)
pin(1595, 1460, "VA + OE 3V3", "right", PURPLE, 21)
pin(1595, 1515, "A1 ← GPIO15", "right", GREEN, 21)
pin(1595, 1570, "B1 → серво", "right", GREEN, 21)
line([(1240, 990), (1240, 1350), (1660, 1350), (1660, 1460), (1595, 1460)], PURPLE, 7)
line([(1940, 800), (2010, 800), (2010, 1355), (1700, 1355), (1700, 1515), (1595, 1515)], GREEN, 7)
line([(1110, 1545), (1070, 1545), (1070, 1710)], BLACK, 7)

rect(650, 1390, 280, 215, "#28333d", 28)
add('<circle cx="790" cy="1488" r="70" fill="#a0abb2" stroke="#596873" stroke-width="5"/>')
add('<path d="M790 1488 L865 1425" stroke="#e1e5e9" stroke-width="14" stroke-linecap="round"/>')
txt(790, 1580, "СЕРВОПРИВІД", 23, "#ffffff", 700, "middle")
pin(650, 1445, "+5 В", "left", ORANGE, 21)
pin(650, 1500, "GND", "left", BLACK, 21)
pin(650, 1550, "сигнал", "left", GREEN, 21)
line([(1080, 1380), (1000, 1380), (1000, 1360), (625, 1360), (625, 1445), (650, 1445)], ORANGE, 7)
line([(650, 1500), (610, 1500), (610, 1710)], BLACK, 7)
line([(1595, 1570), (1620, 1570), (1620, 1640), (610, 1640), (610, 1550), (650, 1550)], GREEN, 7)

# RGB status LEDs at left side ESP: two separate resistors, common cathode.
rect(1870, 1390, 380, 220, "#33433e", 18)
txt(2060, 1435, "RGB LED", 27, "#ffffff", 700, "middle")
add('<circle cx="2045" cy="1500" r="49" fill="#e9ebdd" stroke="#9aa5a2" stroke-width="5"/>')
pin(1870, 1485, "R • резистор", "left", RED, 22)
pin(1870, 1540, "G • резистор", "left", GREEN, 22)
pin(1870, 1590, "спільний −", "left", BLACK, 22)
line([(1940, 870), (2020, 870), (2020, 1290), (1830, 1290), (1830, 1485), (1870, 1485)], RED, 7)
line([(1940, 940), (2070, 940), (2070, 1320), (1800, 1320), (1800, 1540), (1870, 1540)], GREEN, 7)
line([(1870, 1590), (1800, 1590), (1800, 1710)], BLACK, 7)

# Future sensor contacts: shown unconnected, because the shunt location and microphone model are undecided.
rect(2120, 760, 390, 390, "#eef2f5", 18, "#8b99a7", 3)
txt(2315, 815, "РЕЗЕРВ", 30, "#496174", 700, "middle")
txt(2155, 868, "INA226 (на фото)", 24, "#344d60")
txt(2155, 911, "GPIO8  → SDA", 23, "#344d60")
txt(2155, 949, "GPIO9  → SCL", 23, "#344d60")
txt(2155, 988, "3V3 + спільний GND", 22, "#344d60")
txt(2155, 1050, "Мікрофон: модель уточнити", 22, "#344d60")
txt(2155, 1090, "I²C або I²S — залежить від нього", 20, "#344d60")
line([(1940, 1010), (2070, 1010), (2070, 910), (2120, 910)], "#8b99a7", 5, "12 12")
line([(1940, 1080), (2090, 1080), (2090, 950), (2120, 950)], "#8b99a7", 5, "12 12")

txt(90, 1780, "Перед пайкою: мультиметром виставити 5,0 В і 12,0 В; звірити написи контактів на власних модулях.", 26, "#293f51", 600)
txt(90, 1820, "Пунктир = резерв, наразі не підключати. Місце шунта INA226 визначити перед монтажем.", 23, "#506477", 500)
add('</svg>')
OUT.write_text("\n".join(parts), encoding="utf-8")
print(OUT)

# Raster preview using Pillow, so the image can be checked without a browser.
from PIL import Image, ImageDraw, ImageFont
from xml.etree import ElementTree as ET
import re

img = Image.new("RGB", (2600, 1850), "#f6f8fa")
draw = ImageDraw.Draw(img)
regular = "C:/Windows/Fonts/arial.ttf"
bold = "C:/Windows/Fonts/arialbd.ttf"
for el in ET.parse(OUT).getroot():
    tag = el.tag.rsplit("}", 1)[-1]
    a = el.attrib
    if tag == "rect":
        box = tuple(int(float(a[k])) for k in ("x", "y", "width", "height"))
        box = (box[0], box[1], box[0] + box[2], box[1] + box[3])
        fill = a.get("fill") if a.get("fill") != "none" else None
        outline = a.get("stroke") if a.get("stroke") != "none" else None
        draw.rounded_rectangle(box, radius=int(float(a.get("rx", 0))), fill=fill,
                               outline=outline, width=int(float(a.get("stroke-width", 1))))
    elif tag == "circle":
        x, y, r = (int(float(a[k])) for k in ("cx", "cy", "r"))
        fill = a.get("fill") if a.get("fill") != "none" else None
        outline = a.get("stroke") if a.get("stroke") != "none" else None
        draw.ellipse((x-r, y-r, x+r, y+r), fill=fill, outline=outline,
                     width=int(float(a.get("stroke-width", 1))))
    elif tag == "polyline":
        xy = [(int(x), int(y)) for x, y in re.findall(r"([0-9]+),([0-9]+)", a["points"])]
        if a.get("stroke-dasharray"):
            for j in range(len(xy)-1):
                x0, y0 = xy[j]; x1, y1 = xy[j+1]
                length = abs(x1-x0) + abs(y1-y0)
                for start in range(0, length, 24):
                    end = min(start+12, length)
                    pa = (x0+(x1-x0)*start/length, y0+(y1-y0)*start/length)
                    pb = (x0+(x1-x0)*end/length, y0+(y1-y0)*end/length)
                    draw.line((pa, pb), fill=a["stroke"], width=int(a["stroke-width"]))
        else:
            draw.line(xy, fill=a["stroke"], width=int(a["stroke-width"]), joint="curve")
    elif tag == "path":
        xy = [(int(x), int(y)) for x, y in re.findall(r"([0-9]+) ([0-9]+)", a["d"])]
        if len(xy) >= 2:
            draw.line(xy, fill=a["stroke"], width=int(a["stroke-width"]))
    elif tag == "text":
        size = int(float(a["font-size"]))
        font = ImageFont.truetype(bold if int(a.get("font-weight", 400)) >= 600 else regular, size)
        x, y = float(a["x"]), float(a["y"])
        anchor = a.get("text-anchor", "start")
        box = draw.textbbox((0, 0), el.text or "", font=font)
        w = box[2] - box[0]
        if anchor == "middle": x -= w / 2
        if anchor == "end": x -= w
        draw.text((x, y - size), el.text or "", fill=a.get("fill", "#1b2d40"), font=font)
png = OUT.with_suffix(".png")
img.save(png)
print(png)
