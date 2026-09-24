from pathlib import Path
import math

from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, landscape, portrait
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

from build_visio_package import parse_models, edge_anchor


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "output" / "pdf" / "TX_RX_STM_严格源码流程图_完整回答版.pdf"
FONT = "MicrosoftYaHei"
pdfmetrics.registerFont(TTFont(FONT, r"C:\Windows\Fonts\msyh.ttc"))

INK = colors.HexColor("#26364A")
TEXT = colors.HexColor("#172033")
MUTED = colors.HexColor("#596779")
LANE_BG = colors.HexColor("#F8FAFC")
LANE_LINE = colors.HexColor("#CBD5E1")
HEADER = colors.HexColor("#334155")

STYLES = {
    "start": (colors.HexColor("#E2F0D9"), colors.HexColor("#548235")),
    "process": (colors.HexColor("#DDEBF7"), colors.HexColor("#2F5597")),
    "decision": (colors.HexColor("#FFF2CC"), colors.HexColor("#BF9000")),
    "event": (colors.HexColor("#E4DFEC"), colors.HexColor("#7030A0")),
    "state": (colors.HexColor("#E2F0D9"), colors.HexColor("#548235")),
    "error": (colors.HexColor("#F4CCCC"), colors.HexColor("#9C0006")),
    "note": (colors.HexColor("#F2F2F2"), colors.HexColor("#7F7F7F")),
}

SOURCES = {
    "TX发送端": "TX main.c / image.c",
    "RX接收端": "RX main.c / antenna_manager.c / radio_link.c / image.c / uart_bridge.c",
    "STM32主控": "STM main.c / app_uart_rx.c / capsule_protocol.c / sd_storage.c",
    "TX-RX-STM总体链路": "当前工程主链路：TX -> 2.4GHz -> RX -> UART -> STM -> SD/PC",
}


def wrap_text(text: str, max_units: int) -> list[str]:
    lines: list[str] = []
    for paragraph in text.split("\n"):
        current = ""
        units = 0
        for ch in paragraph:
            width = 1 if ord(ch) < 128 else 2
            if current and units + width > max_units:
                lines.append(current)
                current = ch
                units = width
            else:
                current += ch
                units += width
        lines.append(current)
    return lines or [""]


def draw_centered_text(c, text, x, y, w, h, font_size, color=TEXT):
    max_units = max(8, int(w / (font_size * 0.50)))
    lines = wrap_text(text, max_units)
    leading = font_size * 1.26
    while len(lines) * leading > h - 5 and font_size > 5.0:
        font_size -= 0.3
        leading = font_size * 1.24
        max_units = max(8, int(w / (font_size * 0.50)))
        lines = wrap_text(text, max_units)
    start_y = y + h / 2 + (len(lines) - 1) * leading / 2 - font_size * 0.35
    c.setFont(FONT, font_size)
    c.setFillColor(color)
    for index, line in enumerate(lines):
        c.drawCentredString(x + w / 2, start_y - index * leading, line)


def arrow_head(c, x, y, angle, size=5.5):
    spread = 0.55
    c.setFillColor(INK)
    path = c.beginPath()
    path.moveTo(x, y)
    path.lineTo(x - size * math.cos(angle - spread), y - size * math.sin(angle - spread))
    path.lineTo(x - size * math.cos(angle + spread), y - size * math.sin(angle + spread))
    path.close()
    c.drawPath(path, fill=1, stroke=0)


def draw_poly_arrow(c, points, label=""):
    c.setStrokeColor(INK)
    c.setLineWidth(0.85)
    for p1, p2 in zip(points, points[1:]):
        c.line(p1[0], p1[1], p2[0], p2[1])
    p1, p2 = points[-2], points[-1]
    arrow_head(c, p2[0], p2[1], math.atan2(p2[1] - p1[1], p2[0] - p1[0]))
    if label:
        middle = points[len(points) // 2]
        c.setFont(FONT, 5.7)
        c.setFillColor(MUTED)
        c.drawCentredString(middle[0], middle[1] + 3, label)


def render_model(c, model, page_no, total_pages):
    page_size = landscape(A3) if model.width / model.height > 1.05 else portrait(A3)
    page_w, page_h = page_size
    c.setPageSize(page_size)

    # Use the real drawing bounds instead of the nominal Visio page size.  The
    # overview's PC box intentionally extends a little beyond the nominal width.
    min_x = min([0.0] + [n.x - n.w / 2 for n in model.nodes])
    max_x = max([model.width] + [n.x + n.w / 2 for n in model.nodes])
    min_y = min([0.0] + [n.y - n.h / 2 for n in model.nodes])
    max_y = max([model.height] + [n.y + n.h / 2 for n in model.nodes])
    drawing_w = max_x - min_x
    drawing_h = max_y - min_y
    margin_x = 28
    bottom_margin = 25
    top_margin = 48
    scale = min((page_w - margin_x * 2) / drawing_w,
                (page_h - bottom_margin - top_margin) / drawing_h)
    origin_x = margin_x - min_x * scale
    origin_y = bottom_margin - min_y * scale

    def tx(x):
        return origin_x + x * scale

    def ty(y):
        return origin_y + y * scale

    # Page background and title zone.
    c.setFillColor(colors.white)
    c.rect(0, 0, page_w, page_h, fill=1, stroke=0)
    c.setFillColor(HEADER)
    c.rect(0, page_h - 42, page_w, 42, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont(FONT, 16 if page_size[0] < page_size[1] else 18)
    title = f"{model.name}执行流程" if model.name != "TX-RX-STM总体链路" else "TX-RX-STM总体数据与控制链路"
    c.drawString(22, page_h - 27, title)
    c.setFont(FONT, 7.5)
    c.drawRightString(page_w - 22, page_h - 25, "严格按照当前工程源码执行顺序")

    # Lanes sit behind all connectors and nodes.
    for header in model.headers:
        x = tx(header.x - header.w / 2)
        w = header.w * scale
        top = ty(header.y + 0.26)
        bottom = origin_y + 0.55 * scale
        c.setFillColor(LANE_BG)
        c.setStrokeColor(LANE_LINE)
        c.setLineWidth(0.8)
        c.roundRect(x, bottom, w, max(20, top - bottom), 5, fill=1, stroke=1)

    node_map = {node.ident: node for node in model.nodes}

    # Connectors are drawn first so boxes remain readable over crossings.
    for edge in model.edges:
        if edge.source not in node_map or edge.target not in node_map:
            continue
        source = node_map[edge.source]
        target = node_map[edge.target]
        x1, y1, x2, y2 = edge_anchor(source, target)
        start = (tx(x1), ty(y1))
        end = (tx(x2), ty(y2))
        if target.y > source.y + 0.1:
            # Backward loop: route around the left side of the involved boxes.
            route_x = tx(min(source.x - source.w / 2, target.x - target.w / 2) - 0.22)
            points = [start, (route_x, start[1]), (route_x, end[1]), end]
        elif abs(start[0] - end[0]) < 2 or abs(start[1] - end[1]) < 2:
            points = [start, end]
        else:
            mid_y = (start[1] + end[1]) / 2
            points = [start, (start[0], mid_y), (end[0], mid_y), end]
        draw_poly_arrow(c, points, edge.label)

    # Main nodes.
    for node in model.nodes:
        x = tx(node.x - node.w / 2)
        y = ty(node.y - node.h / 2)
        w = node.w * scale
        h = node.h * scale
        fill, line = STYLES.get(node.kind, STYLES["process"])
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.0)
        radius = min(7, h / 2) if node.kind == "start" else min(5, h / 4)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
        base_size = 7.2 if page_size[0] < page_size[1] else 7.8
        if node.h > 1.2:
            base_size = 7.6 if page_size[0] < page_size[1] else 8.2
        draw_centered_text(c, node.text, x + 3, y + 2, w - 6, h - 4, base_size)

    # Lane headers.
    for header in model.headers:
        x = tx(header.x - header.w / 2)
        y = ty(header.y - 0.26)
        w = header.w * scale
        h = 0.52 * scale
        c.setFillColor(HEADER)
        c.setStrokeColor(HEADER)
        c.roundRect(x, y, w, h, 4, fill=1, stroke=0)
        draw_centered_text(c, header.text, x + 3, y + 1, w - 6, h - 2, 9.0, colors.white)

    c.setFillColor(MUTED)
    c.setFont(FONT, 6.5)
    c.drawString(20, 11, f"依据：{SOURCES[model.name]}")
    c.drawRightString(page_w - 20, 11, f"第 {page_no} 页 / 共 {total_pages} 页")
    c.showPage()


def lane_for_node(model, node):
    """Assign side-branch nodes to the nearest logical swim lane."""
    return min(range(len(model.headers)), key=lambda i: abs(node.x - model.headers[i].x))


def render_lane(c, model, lane_index, page_no, total_pages):
    """Render one source-code swim lane per page for maximum readability."""
    page_w, page_h = portrait(A3)
    c.setPageSize((page_w, page_h))
    c.setFillColor(colors.white)
    c.rect(0, 0, page_w, page_h, fill=1, stroke=0)

    header = model.headers[lane_index]
    nodes = [n for n in model.nodes if lane_for_node(model, n) == lane_index]
    node_map = {n.ident: n for n in nodes}
    internal_edges = [e for e in model.edges if e.source in node_map and e.target in node_map]
    cross_edges = [e for e in model.edges if (e.source in node_map) ^ (e.target in node_map)]

    c.setFillColor(HEADER)
    c.rect(0, page_h - 46, page_w, 46, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont(FONT, 16)
    c.drawString(22, page_h - 29, f"{model.name}：{header.text}")
    c.setFont(FONT, 7.5)
    c.drawRightString(page_w - 22, page_h - 27, "严格按照当前工程源码执行顺序")

    # Reflow the original Visio coordinates into collision-free source-order
    # rows.  Nodes that share a source-code branch level remain side by side.
    rows = []
    for node in sorted(nodes, key=lambda n: -n.y):
        if not rows or abs(rows[-1][0].y - node.y) > 0.32:
            rows.append([node])
        else:
            rows[-1].append(node)
    for row in rows:
        row.sort(key=lambda n: n.x)

    content_left, content_right = 54, page_w - 54
    content_bottom, content_top = 61, page_h - 71
    layout = {}
    row_specs = []
    for row in rows:
        count = len(row)
        gap_x = 18
        width = min(430, content_right - content_left) if count == 1 else (
            (content_right - content_left - gap_x * (count - 1)) / count
        )
        max_lines = 1
        for node in row:
            max_lines = max(max_lines, len(wrap_text(node.text, max(15, int(width / 5.0)))))
        height = min(62, max(38, 28 + max_lines * 10))
        row_specs.append((row, width, height, gap_x))

    total_heights = sum(spec[2] for spec in row_specs)
    gap_y = 8 if len(rows) <= 1 else max(6, min(16, (
        content_top - content_bottom - total_heights
    ) / (len(rows) - 1)))
    cursor_top = content_top
    for row, width, height, gap_x in row_specs:
        count = len(row)
        total_width = count * width + (count - 1) * gap_x
        start_x = (page_w - total_width) / 2
        y = cursor_top - height
        for idx, node in enumerate(row):
            layout[node.ident] = (start_x + idx * (width + gap_x), y, width, height)
        cursor_top = y - gap_y

    # Soft lane background.
    c.setFillColor(LANE_BG)
    c.setStrokeColor(LANE_LINE)
    c.roundRect(27, 49, page_w - 54, page_h - 112, 7, fill=1, stroke=1)

    for edge in internal_edges:
        sx, sy, sw, sh = layout[edge.source]
        tx_, ty_, tw, th = layout[edge.target]
        scx, scy = sx + sw / 2, sy + sh / 2
        tcx, tcy = tx_ + tw / 2, ty_ + th / 2
        if tcy < scy - 4:
            start, end = (scx, sy), (tcx, ty_ + th)
            if abs(scx - tcx) < 3:
                points = [start, end]
            else:
                mid_y = (start[1] + end[1]) / 2
                points = [start, (start[0], mid_y), (end[0], mid_y), end]
        elif tcy > scy + 4:
            start, end = (sx, scy), (tx_, tcy)
            route_x = max(35, min(sx, tx_) - 16)
            points = [start, (route_x, start[1]), (route_x, end[1]), end]
        else:
            if tcx >= scx:
                start, end = (sx + sw, scy), (tx_, tcy)
            else:
                start, end = (sx, scy), (tx_ + tw, tcy)
            points = [start, end]
        draw_poly_arrow(c, points, edge.label)

    for node in nodes:
        x, y, w, h = layout[node.ident]
        fill, line = STYLES.get(node.kind, STYLES["process"])
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.05)
        radius = min(8, h / 2) if node.kind == "start" else min(6, h / 4)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
        draw_centered_text(c, node.text, x + 4, y + 3, w - 8, h - 6, 8.4)

    # Preserve the cross-lane relationships as a compact page-reference note.
    if cross_edges:
        all_nodes = {n.ident: n for n in model.nodes}
        relations = []
        for edge in cross_edges:
            src = all_nodes.get(edge.source)
            dst = all_nodes.get(edge.target)
            if src and dst:
                relations.append(f"{src.text.splitlines()[0]} → {dst.text.splitlines()[0]}")
        note = "跨页调用：" + "；".join(relations[:4])
        c.setFillColor(MUTED)
        c.setFont(FONT, 6.2)
        c.drawString(31, 37, note[:125])

    c.setFillColor(MUTED)
    c.setFont(FONT, 6.5)
    c.drawString(20, 11, f"依据：{SOURCES[model.name]}")
    c.drawRightString(page_w - 20, 11, f"第 {page_no} 页 / 共 {total_pages} 页")
    c.showPage()


def build_pdf():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    models = parse_models()
    c = canvas.Canvas(str(OUTPUT), pagesize=portrait(A3), pageCompression=1)
    c.setTitle("TX RX STM 严格源码流程图 - 完整回答版")
    c.setAuthor("Codex")
    detailed = models[:3]
    overview = models[3]
    total_pages = sum(len(model.headers) for model in detailed) + 1
    page_no = 1
    for model in detailed:
        for lane_index in range(len(model.headers)):
            render_lane(c, model, lane_index, page_no, total_pages)
            page_no += 1
    render_model(c, overview, page_no, total_pages)
    c.save()
    print(OUTPUT)


if __name__ == "__main__":
    build_pdf()
