from __future__ import annotations

import html
import re
import shutil
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from xml.etree import ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "generate_visio_flows.ps1"
OUTPUT = ROOT / "doc" / "TX_RX_STM_代码执行流程图.vsdx"
TEMPLATE = Path(
    r"C:\Program Files\Microsoft Office\root\Office16\Visio Content\2052\NEW_BASFLO_M.VSTX"
)

VIS = "http://schemas.microsoft.com/office/visio/2012/main"
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG_REL = "http://schemas.openxmlformats.org/package/2006/relationships"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
APP = "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"
VT = "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes"


@dataclass
class Node:
    ident: str
    text: str
    x: float
    y: float
    w: float
    h: float
    kind: str


@dataclass
class Header:
    text: str
    x: float
    y: float
    w: float


@dataclass
class Edge:
    source: str
    target: str
    label: str = ""


@dataclass
class PageModel:
    function: str
    name: str
    width: float
    height: float
    nodes: list[Node]
    headers: list[Header]
    edges: list[Edge]


def quoted(pattern: str) -> str:
    return rf"(?:'(?P<{pattern}_s>[^']*)'|\"(?P<{pattern}_d>[^\"]*)\")"


def decode_text(value: str) -> str:
    return value.replace(r"\n", "\n").replace(r"\t", " ")


def parse_models() -> list[PageModel]:
    source = SOURCE.read_text(encoding="utf-8-sig")
    names = [
        ("Build-TxPage", "TX发送端"),
        ("Build-RxPage", "RX接收端"),
        ("Build-StmPage", "STM32主控"),
        ("Build-OverviewPage", "TX-RX-STM总体链路"),
    ]
    models: list[PageModel] = []

    for index, (function, fallback_name) in enumerate(names):
        start = source.index(f"function {function}")
        end = source.find("\nfunction ", start + 1)
        if end < 0:
            end = source.index("\n$resolvedOutput", start)
        block = source[start:end]

        config = re.search(
            r"Configure-Page \$Page '([^']+)' ([0-9.]+) ([0-9.]+)", block
        )
        name = config.group(1) if config else fallback_name
        width = float(config.group(2)) if config else 19.0
        height = float(config.group(3)) if config else 13.0

        node_re = re.compile(
            r"Add-Node \$Page \$nodes '([^']+)'\s+"
            + quoted("text")
            + r"\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+(\w+)"
        )
        nodes: list[Node] = []
        for match in node_re.finditer(block):
            text = match.group("text_s")
            if text is None:
                text = match.group("text_d")
            nodes.append(
                Node(
                    match.group(1),
                    decode_text(text or ""),
                    float(match.group(4)),
                    float(match.group(5)),
                    float(match.group(6)),
                    float(match.group(7)),
                    match.group(8),
                )
            )

        header_re = re.compile(
            r"Add-Header \$Page '([^']+)' ([0-9.]+) ([0-9.]+) ([0-9.]+)"
        )
        headers = [
            Header(m.group(1), float(m.group(2)), float(m.group(3)), float(m.group(4)))
            for m in header_re.finditer(block)
        ]

        edge_re = re.compile(
            r"Add-Edge \$Page \$nodes ([A-Za-z0-9_]+) ([A-Za-z0-9_]+)(?: '([^']*)')?"
        )
        edges = [Edge(m.group(1), m.group(2), m.group(3) or "") for m in edge_re.finditer(block)]
        for m in re.finditer(r"@\('([^']+)','([^']+)'\)", block):
            edges.append(Edge(m.group(1), m.group(2), ""))

        # Remove duplicate edges while preserving their source order.
        unique: list[Edge] = []
        seen: set[tuple[str, str, str]] = set()
        for edge in edges:
            key = (edge.source, edge.target, edge.label)
            if key not in seen:
                unique.append(edge)
                seen.add(key)
        models.append(PageModel(function, name, width, height, nodes, headers, unique))
    return models


def cell(parent: ET.Element, name: str, value: str | float, unit: str | None = None, formula: str | None = None):
    attributes = {"N": name, "V": str(value)}
    if unit:
        attributes["U"] = unit
    if formula:
        attributes["F"] = formula
    ET.SubElement(parent, f"{{{VIS}}}Cell", attributes)


KIND_STYLE = {
    "start": ("#E2EFDA", "#548235"),
    "process": ("#DDEBF7", "#2F5496"),
    "decision": ("#FFF2CC", "#BF9000"),
    "event": ("#EAD1DC", "#800040"),
    "state": ("#E2EFDA", "#548235"),
    "error": ("#F4CCCC", "#990000"),
    "note": ("#F2F2F2", "#7F7F7F"),
}


def add_text_sections(shape: ET.Element, size: float = 0.118, bold: bool = False, color: str = "#1F1F1F"):
    character = ET.SubElement(shape, f"{{{VIS}}}Section", {"N": "Character"})
    row = ET.SubElement(character, f"{{{VIS}}}Row", {"IX": "0"})
    cell(row, "Font", "Microsoft YaHei")
    cell(row, "AsianFont", "Microsoft YaHei")
    cell(row, "Size", size)
    cell(row, "Style", "1" if bold else "0")
    cell(row, "Color", color)
    paragraph = ET.SubElement(shape, f"{{{VIS}}}Section", {"N": "Paragraph"})
    prow = ET.SubElement(paragraph, f"{{{VIS}}}Row", {"IX": "0"})
    cell(prow, "HorzAlign", "1")
    cell(prow, "SpLine", "-1.1")


def add_rectangle_geometry(shape: ET.Element, width: float, height: float):
    section = ET.SubElement(shape, f"{{{VIS}}}Section", {"N": "Geometry", "IX": "0"})
    cell(section, "NoFill", "0")
    cell(section, "NoLine", "0")
    points = [(0, 0, "MoveTo"), (width, 0, "LineTo"), (width, height, "LineTo"), (0, height, "LineTo"), (0, 0, "LineTo")]
    for ix, (x, y, kind) in enumerate(points, 1):
        row = ET.SubElement(section, f"{{{VIS}}}Row", {"IX": str(ix), "T": kind})
        cell(row, "X", x)
        cell(row, "Y", y)


def add_shape(shapes: ET.Element, shape_id: int, node: Node):
    fill, line = KIND_STYLE.get(node.kind, KIND_STYLE["process"])
    shape = ET.SubElement(
        shapes,
        f"{{{VIS}}}Shape",
        {"ID": str(shape_id), "NameU": node.ident, "Name": node.ident, "Type": "Shape", "LineStyle": "0", "FillStyle": "0", "TextStyle": "0"},
    )
    cell(shape, "PinX", node.x)
    cell(shape, "PinY", node.y)
    cell(shape, "Width", node.w)
    cell(shape, "Height", node.h)
    cell(shape, "LocPinX", node.w / 2, formula="Width*0.5")
    cell(shape, "LocPinY", node.h / 2, formula="Height*0.5")
    cell(shape, "Angle", "0")
    cell(shape, "FillForegnd", fill)
    cell(shape, "FillBkgnd", "#FFFFFF")
    cell(shape, "FillPattern", "1")
    cell(shape, "LineColor", line)
    cell(shape, "LinePattern", "1")
    cell(shape, "LineWeight", "0.012")
    cell(shape, "Rounding", "0.08")
    cell(shape, "VerticalAlign", "1")
    cell(shape, "LeftMargin", "0.08")
    cell(shape, "RightMargin", "0.08")
    cell(shape, "TopMargin", "0.04")
    cell(shape, "BottomMargin", "0.04")
    add_text_sections(shape, 0.112 if node.h < 1.0 else 0.105)
    add_rectangle_geometry(shape, node.w, node.h)
    text = ET.SubElement(shape, f"{{{VIS}}}Text")
    text.text = node.text


def add_header_shape(shapes: ET.Element, shape_id: int, header: Header):
    node = Node(f"Header{shape_id}", header.text, header.x, header.y, header.w, 0.52, "process")
    shape = ET.SubElement(
        shapes,
        f"{{{VIS}}}Shape",
        {"ID": str(shape_id), "NameU": node.ident, "Name": node.ident, "Type": "Shape", "LineStyle": "0", "FillStyle": "0", "TextStyle": "0"},
    )
    for name, value in [("PinX", node.x), ("PinY", node.y), ("Width", node.w), ("Height", node.h)]:
        cell(shape, name, value)
    cell(shape, "LocPinX", node.w / 2, formula="Width*0.5")
    cell(shape, "LocPinY", node.h / 2, formula="Height*0.5")
    cell(shape, "FillForegnd", "#44546A")
    cell(shape, "FillPattern", "1")
    cell(shape, "LineColor", "#44546A")
    cell(shape, "VerticalAlign", "1")
    add_text_sections(shape, 0.15, True, "#FFFFFF")
    add_rectangle_geometry(shape, node.w, node.h)
    ET.SubElement(shape, f"{{{VIS}}}Text").text = node.text


def add_title_shape(shapes: ET.Element, shape_id: int, model: PageModel):
    shape = ET.SubElement(shapes, f"{{{VIS}}}Shape", {"ID": str(shape_id), "NameU": "Title", "Type": "Shape", "LineStyle": "0", "FillStyle": "0", "TextStyle": "0"})
    cell(shape, "PinX", model.width / 2)
    cell(shape, "PinY", model.height - 0.60)
    cell(shape, "Width", model.width - 1.0)
    cell(shape, "Height", "0.7")
    cell(shape, "LocPinX", (model.width - 1.0) / 2, formula="Width*0.5")
    cell(shape, "LocPinY", "0.35", formula="Height*0.5")
    cell(shape, "FillPattern", "0")
    cell(shape, "LinePattern", "0")
    cell(shape, "VerticalAlign", "1")
    add_text_sections(shape, 0.23, True)
    add_rectangle_geometry(shape, model.width - 1.0, 0.7)
    ET.SubElement(shape, f"{{{VIS}}}Text").text = f"{model.name}（按当前源码执行顺序）"


def edge_anchor(source: Node, target: Node) -> tuple[float, float, float, float]:
    dx = target.x - source.x
    dy = target.y - source.y
    if abs(dy) >= abs(dx):
        if dy < 0:
            return source.x, source.y - source.h / 2, target.x, target.y + target.h / 2
        return source.x, source.y + source.h / 2, target.x, target.y - target.h / 2
    if dx > 0:
        return source.x + source.w / 2, source.y, target.x - target.w / 2, target.y
    return source.x - source.w / 2, source.y, target.x + target.w / 2, target.y


def add_connector(shapes: ET.Element, shape_id: int, edge: Edge, node_map: dict[str, Node]):
    if edge.source not in node_map or edge.target not in node_map:
        return
    x1, y1, x2, y2 = edge_anchor(node_map[edge.source], node_map[edge.target])
    min_x, min_y = min(x1, x2), min(y1, y2)
    width, height = max(abs(x2 - x1), 0.01), max(abs(y2 - y1), 0.01)
    shape = ET.SubElement(shapes, f"{{{VIS}}}Shape", {"ID": str(shape_id), "NameU": f"Connector.{shape_id}", "Type": "Shape", "LineStyle": "0", "FillStyle": "0", "TextStyle": "0"})
    cell(shape, "PinX", min_x)
    cell(shape, "PinY", min_y)
    cell(shape, "Width", width)
    cell(shape, "Height", height)
    cell(shape, "LocPinX", "0")
    cell(shape, "LocPinY", "0")
    cell(shape, "LineColor", "#595959")
    cell(shape, "LineWeight", "0.018")
    cell(shape, "LinePattern", "1")
    cell(shape, "EndArrow", "4")
    cell(shape, "EndArrowSize", "2")
    section = ET.SubElement(shape, f"{{{VIS}}}Section", {"N": "Geometry", "IX": "0"})
    cell(section, "NoFill", "1")
    cell(section, "NoLine", "0")
    local_x1, local_y1 = x1 - min_x, y1 - min_y
    local_x2, local_y2 = x2 - min_x, y2 - min_y
    for ix, (kind, x, y, fx, fy) in enumerate(
        [
            ("MoveTo", local_x1, local_y1, None, None),
            ("LineTo", (local_x1 + local_x2) / 2, local_y1, None, None),
            ("LineTo", (local_x1 + local_x2) / 2, local_y2, None, None),
            ("LineTo", local_x2, local_y2, None, None),
        ],
        1,
    ):
        row = ET.SubElement(section, f"{{{VIS}}}Row", {"IX": str(ix), "T": kind})
        cell(row, "X", x, formula=fx)
        cell(row, "Y", y, formula=fy)
    if edge.label:
        label_x = (x1 + x2) / 2 + (0.34 if abs(y2 - y1) >= abs(x2 - x1) else 0)
        label_y = (y1 + y2) / 2 + (0.22 if abs(x2 - x1) > abs(y2 - y1) else 0)
        label = ET.SubElement(shapes, f"{{{VIS}}}Shape", {"ID": str(10000 + shape_id), "NameU": f"EdgeLabel.{shape_id}", "Type": "Shape", "LineStyle": "0", "FillStyle": "0", "TextStyle": "0"})
        cell(label, "PinX", label_x)
        cell(label, "PinY", label_y)
        cell(label, "Width", "1.45")
        cell(label, "Height", "0.34")
        cell(label, "LocPinX", "0.725", formula="Width*0.5")
        cell(label, "LocPinY", "0.17", formula="Height*0.5")
        cell(label, "FillPattern", "0")
        cell(label, "LinePattern", "0")
        cell(label, "VerticalAlign", "1")
        add_text_sections(label, 0.09)
        add_rectangle_geometry(label, 1.45, 0.34)
        ET.SubElement(label, f"{{{VIS}}}Text").text = edge.label


def page_xml(model: PageModel) -> bytes:
    root = ET.Element(f"{{{VIS}}}PageContents", {f"{{http://www.w3.org/XML/1998/namespace}}space": "preserve"})
    shapes = ET.SubElement(root, f"{{{VIS}}}Shapes")
    shape_id = 1
    add_title_shape(shapes, shape_id, model)
    shape_id += 1
    for header in model.headers:
        add_header_shape(shapes, shape_id, header)
        shape_id += 1
    node_map = {node.ident: node for node in model.nodes}
    for node in model.nodes:
        add_shape(shapes, shape_id, node)
        shape_id += 1
    for edge in model.edges:
        add_connector(shapes, shape_id, edge, node_map)
        shape_id += 1
    ET.register_namespace("", VIS)
    ET.register_namespace("r", REL)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def pages_index_xml(models: list[PageModel]) -> bytes:
    root = ET.Element(f"{{{VIS}}}Pages", {f"{{http://www.w3.org/XML/1998/namespace}}space": "preserve"})
    for ix, model in enumerate(models):
        page = ET.SubElement(root, f"{{{VIS}}}Page", {"ID": str(ix), "NameU": model.name, "Name": model.name, "ViewScale": "-1", "ViewCenterX": str(model.width / 2), "ViewCenterY": str(model.height / 2)})
        sheet = ET.SubElement(page, f"{{{VIS}}}PageSheet", {"LineStyle": "0", "FillStyle": "0", "TextStyle": "0"})
        cell(sheet, "PageWidth", model.width)
        cell(sheet, "PageHeight", model.height)
        cell(sheet, "PageScale", "1")
        cell(sheet, "DrawingScale", "1")
        cell(sheet, "DrawingSizeType", "0")
        cell(sheet, "DrawingScaleType", "0")
        cell(sheet, "PlaceStyle", "2")
        cell(sheet, "RouteStyle", "16")
        cell(sheet, "DrawingResizeType", "0")
        ET.SubElement(page, f"{{{VIS}}}Rel", {f"{{{REL}}}id": f"rId{ix + 1}"})
    ET.register_namespace("", VIS)
    ET.register_namespace("r", REL)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def pages_rels_xml(models: list[PageModel]) -> bytes:
    root = ET.Element(f"{{{PKG_REL}}}Relationships")
    for ix, _ in enumerate(models):
        ET.SubElement(root, f"{{{PKG_REL}}}Relationship", {"Id": f"rId{ix + 1}", "Type": "http://schemas.microsoft.com/visio/2010/relationships/page", "Target": f"page{ix + 1}.xml"})
    ET.register_namespace("", PKG_REL)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def patch_content_types(data: bytes, models: list[PageModel]) -> bytes:
    root = ET.fromstring(data)
    for override in root.findall(f"{{{CT}}}Override"):
        if override.attrib.get("PartName") == "/visio/document.xml":
            override.set("ContentType", "application/vnd.ms-visio.drawing.main+xml")
    existing = {x.attrib.get("PartName") for x in root.findall(f"{{{CT}}}Override")}
    for ix in range(1, len(models) + 1):
        name = f"/visio/pages/page{ix}.xml"
        if name not in existing:
            ET.SubElement(root, f"{{{CT}}}Override", {"PartName": name, "ContentType": "application/vnd.ms-visio.page+xml"})
    ET.register_namespace("", CT)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def patch_app(data: bytes, models: list[PageModel]) -> bytes:
    root = ET.fromstring(data)
    template = root.find(f"{{{APP}}}Template")
    if template is not None:
        template.text = ""
    heading = root.find(f"{{{APP}}}HeadingPairs")
    if heading is not None:
        vector = heading.find(f"{{{VT}}}vector")
        if vector is not None:
            values = vector.findall(f"{{{VT}}}variant")
            if len(values) > 1:
                count = values[1].find(f"{{{VT}}}i4")
                if count is not None:
                    count.text = str(len(models))
    titles = root.find(f"{{{APP}}}TitlesOfParts")
    if titles is not None:
        vector = titles.find(f"{{{VT}}}vector")
        if vector is not None:
            vector.set("size", str(len(models)))
            for child in list(vector):
                vector.remove(child)
            for model in models:
                ET.SubElement(vector, f"{{{VT}}}lpstr").text = model.name
    ET.register_namespace("", APP)
    ET.register_namespace("vt", VT)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def patch_core(data: bytes) -> bytes:
    text = data.decode("utf-8-sig")
    text = re.sub(r"<dc:title>.*?</dc:title>", "<dc:title>TX RX STM 代码执行流程图</dc:title>", text, flags=re.S)
    return text.encode("utf-8")


def build_package(models: list[PageModel]):
    if not TEMPLATE.exists():
        raise FileNotFoundError(TEMPLATE)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    replacements: dict[str, bytes] = {
        "visio/pages/pages.xml": pages_index_xml(models),
        "visio/pages/_rels/pages.xml.rels": pages_rels_xml(models),
    }
    with zipfile.ZipFile(TEMPLATE, "r") as source_zip:
        replacements["[Content_Types].xml"] = patch_content_types(source_zip.read("[Content_Types].xml"), models)
        replacements["docProps/app.xml"] = patch_app(source_zip.read("docProps/app.xml"), models)
        replacements["docProps/core.xml"] = patch_core(source_zip.read("docProps/core.xml"))
        for ix, model in enumerate(models, 1):
            replacements[f"visio/pages/page{ix}.xml"] = page_xml(model)

        with tempfile.NamedTemporaryFile(delete=False, suffix=".vsdx", dir=OUTPUT.parent) as tmp:
            temp_path = Path(tmp.name)
        try:
            with zipfile.ZipFile(temp_path, "w", compression=zipfile.ZIP_DEFLATED) as output_zip:
                written: set[str] = set()
                for info in source_zip.infolist():
                    name = info.filename
                    if name.startswith("visio/pages/page") and name.endswith(".xml") and name != "visio/pages/pages.xml":
                        continue
                    data = replacements.get(name, source_zip.read(name))
                    output_zip.writestr(name, data)
                    written.add(name)
                for name, data in replacements.items():
                    if name not in written:
                        output_zip.writestr(name, data)
            temp_path.replace(OUTPUT)
        finally:
            if temp_path.exists():
                temp_path.unlink()


def verify(models: list[PageModel]):
    with zipfile.ZipFile(OUTPUT, "r") as archive:
        bad = archive.testzip()
        if bad:
            raise RuntimeError(f"Corrupt ZIP member: {bad}")
        required = {"visio/pages/pages.xml", "[Content_Types].xml"}
        required.update({f"visio/pages/page{i}.xml" for i in range(1, len(models) + 1)})
        missing = required.difference(archive.namelist())
        if missing:
            raise RuntimeError(f"Missing package parts: {sorted(missing)}")
        for name in required:
            ET.fromstring(archive.read(name))
    print(OUTPUT)
    print(f"pages={len(models)} size={OUTPUT.stat().st_size}")
    for model in models:
        print(f"{model.name}: nodes={len(model.nodes)} edges={len(model.edges)}")


if __name__ == "__main__":
    page_models = parse_models()
    build_package(page_models)
    verify(page_models)
