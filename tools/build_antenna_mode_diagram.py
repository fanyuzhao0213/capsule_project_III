"""Render the RX automatic-scan/manual-antenna transition diagram."""

from PIL import Image, ImageDraw, ImageFont


W, H = 1800, 1000
im = Image.new("RGB", (W, H), "#ffffff")
d = ImageDraw.Draw(im)

INK = "#183446"
MUTED = "#607888"
TEAL = "#008796"
TEAL_PALE = "#eff8f9"
TEAL_BORDER = "#b7d6de"
ORANGE = "#c46b2b"
ORANGE_PALE = "#fff6ec"
ORANGE_BORDER = "#e5c49e"
WHITE = "#ffffff"

FONT = r"C:\Windows\Fonts\msyh.ttc"
BOLD = r"C:\Windows\Fonts\msyhbd.ttc"


def f(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)


def text(x, y, s, size=25, color=INK, bold=False):
    d.text((x, y), s, font=f(size, bold), fill=color)


def center(cx, y, s, size=25, color=INK, bold=False):
    box = d.textbbox((0, 0), s, font=f(size, bold))
    text(cx - (box[2] - box[0]) / 2, y, s, size, color, bold)


def round_rect(box, fill, outline, radius=19, width=2):
    d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def arrow(x1, y1, x2, y2, color=TEAL, width=5, head=14):
    d.line((x1, y1, x2, y2), fill=color, width=width)
    if x2 > x1:
        pts = [(x2, y2), (x2 - head, y2 - head * .55),
               (x2 - head, y2 + head * .55)]
    elif x2 < x1:
        pts = [(x2, y2), (x2 + head, y2 - head * .55),
               (x2 + head, y2 + head * .55)]
    elif y2 > y1:
        pts = [(x2, y2), (x2 - head * .55, y2 - head),
               (x2 + head * .55, y2 - head)]
    else:
        pts = [(x2, y2), (x2 - head * .55, y2 + head),
               (x2 + head * .55, y2 + head)]
    d.polygon(pts, fill=color)


def card(x, y, w, h, heading, lines, top=TEAL):
    round_rect((x, y, x + w, y + h), WHITE, TEAL_BORDER)
    d.rounded_rectangle((x + 1, y + 1, x + w - 1, y + 11),
                        radius=5, fill=top)
    text(x + 22, y + 28, heading, 29, INK, True)
    for i, line in enumerate(lines):
        text(x + 22, y + 70 + 30 * i, line, 21, MUTED)


# Heading
text(66, 42, "RX 天线控制", 23, TEAL, True)
text(66, 82, "自动扫描与手动选路的转换逻辑", 48, INK, True)
text(68, 151, "绑定决定能否接收图片；测试命令只改变天线选路方式。", 25, MUTED)

# Automatic path
round_rect((58, 215, 1742, 455), TEAL_PALE, TEAL_BORDER, 23)
d.rounded_rectangle((81, 237, 90, 274), radius=4, fill=TEAL)
text(106, 238, "自动选路", 30, INK, True)
text(286, 244, "未进入测试模式时，由 RX 状态机切换天线", 22, MUTED)

auto = [
    (84, "DISCOVERY", ["未绑定", "逐路发现胶囊"]),
    (495, "SEEK_END", ["已绑定", "寻找目标胶囊 END"]),
    (906, "FAST_SCAN", ["逐路采样", "统计匹配 SN 的 RSSI"]),
    (1317, "LOCKED", ["锁定较优天线", "接收绑定胶囊图片"]),
]
for x, title, lines in auto:
    card(x, 295, 390, 132, title, lines)
for x in (474, 885, 1296):
    arrow(x, 360, x + 21, 360)

# Transition into manual mode
d.line((899, 455, 899, 489), fill=ORANGE, width=5)
arrow(899, 489, 899, 530, ORANGE)
round_rect((651, 470, 1147, 515), ORANGE_PALE, ORANGE_BORDER, 13)
center(899, 476, "PC 下发 0x2C / 00：开始测试", 22, ORANGE, True)

# Manual path
round_rect((58, 535, 1742, 795), "#f0f8fa", TEAL_BORDER, 23)
d.rounded_rectangle((81, 557, 90, 594), radius=4, fill=TEAL)
text(106, 558, "手动测试", 30, INK, True)
text(286, 564, "收到 00 后暂停自动扫描与失联换路", 22, MUTED)

manual = [
    (84, "00  开始", ["设置已开始标志 = 1", "保持当前天线不变"]),
    (495, "01～0C  选路", ["标志 = 1：切到指定天线", "标志 = 0：拒绝，不切换"]),
    (906, "接收与观测", ["绑定图片仍可收", "当前路最新 RSSI；其余为 0"]),
    (1317, "0D  停止", ["清除已开始标志", "重新启动自动选路"]),
]
for x, title, lines in manual:
    card(x, 625, 390, 138, title, lines)
for x in (474, 885, 1296):
    arrow(x, 694, x + 21, 694)

# Stop path: always restart automatic selection according to binding state.
d.line((1512, 763, 1512, 830), fill=TEAL, width=5)
d.line((1512, 830, 899, 830), fill=TEAL, width=5)
arrow(899, 830, 899, 852)
round_rect((560, 852, 1238, 977), WHITE, TEAL_BORDER, 18)
center(899, 864, "停止后按绑定状态恢复", 28, INK, True)
center(899, 910, "已绑定 → SEEK_END → FAST_SCAN → LOCKED", 22, TEAL)
center(899, 943, "未绑定 → DISCOVERY", 22, MUTED)

im.save(r"E:\Project\capsule_project\output\RX天线自动扫描与手动选路转换.png")
