from pathlib import Path
import math

from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "output" / "pdf" / "TX_RX_STM_代码流程学习版.pdf"

PAGE_W, PAGE_H = landscape(A3)
FONT = "MicrosoftYaHei"
pdfmetrics.registerFont(TTFont(FONT, r"C:\Windows\Fonts\msyh.ttc"))

COLORS = {
    "blue": colors.HexColor("#DDEBF7"),
    "blue_line": colors.HexColor("#2F5597"),
    "green": colors.HexColor("#E2F0D9"),
    "green_line": colors.HexColor("#548235"),
    "yellow": colors.HexColor("#FFF2CC"),
    "yellow_line": colors.HexColor("#BF9000"),
    "purple": colors.HexColor("#E4DFEC"),
    "purple_line": colors.HexColor("#7030A0"),
    "red": colors.HexColor("#F4CCCC"),
    "red_line": colors.HexColor("#9C0006"),
    "gray": colors.HexColor("#F2F2F2"),
    "gray_line": colors.HexColor("#7F7F7F"),
    "dark": colors.HexColor("#334155"),
    "text": colors.HexColor("#172033"),
    "muted": colors.HexColor("#5F6B7A"),
}


def lines_for(text, max_chars):
    output = []
    for paragraph in text.split("\n"):
        if not paragraph:
            output.append("")
            continue
        current = ""
        for char in paragraph:
            width = 1 if ord(char) < 128 else 2
            current_width = sum(1 if ord(c) < 128 else 2 for c in current)
            if current and current_width + width > max_chars:
                output.append(current)
                current = char
            else:
                current += char
        if current:
            output.append(current)
    return output


def draw_text(c, text, x, y, w, h, size=8, color=None, bold=False, leading=None):
    color = color or COLORS["text"]
    leading = leading or size * 1.28
    max_chars = max(8, int(w / (size * 0.52)))
    wrapped = lines_for(text, max_chars)
    total = len(wrapped) * leading
    ty = y + (h + total) / 2 - leading * 0.82
    c.setFillColor(color)
    c.setFont(FONT, size)
    for line in wrapped:
        c.drawCentredString(x + w / 2, ty, line)
        ty -= leading


def box(c, x, y, w, h, text, kind="process", size=8, radius=7):
    if kind == "start":
        fill, line = COLORS["green"], COLORS["green_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, h / 2, fill=1, stroke=1)
    elif kind == "decision":
        fill, line = COLORS["yellow"], COLORS["yellow_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    elif kind == "state":
        fill, line = COLORS["green"], COLORS["green_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    elif kind == "event":
        fill, line = COLORS["purple"], COLORS["purple_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    elif kind == "error":
        fill, line = COLORS["red"], COLORS["red_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    elif kind == "note":
        fill, line = COLORS["gray"], COLORS["gray_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    else:
        fill, line = COLORS["blue"], COLORS["blue_line"]
        c.setFillColor(fill)
        c.setStrokeColor(line)
        c.setLineWidth(1.2)
        c.roundRect(x, y, w, h, radius, fill=1, stroke=1)
    draw_text(c, text, x + 5, y + 3, w - 10, h - 6, size=size)
    return (x, y, w, h)


def arrow(c, start, end, label="", color=None, bend=None):
    color = color or COLORS["dark"]
    x1, y1 = start
    x2, y2 = end
    c.setStrokeColor(color)
    c.setFillColor(color)
    c.setLineWidth(1.0)
    if bend is None:
        c.line(x1, y1, x2, y2)
        px, py = x2, y2
        angle = math.atan2(y2 - y1, x2 - x1)
    else:
        bx, by = bend
        c.line(x1, y1, bx, by)
        c.line(bx, by, x2, y2)
        px, py = x2, y2
        angle = math.atan2(y2 - by, x2 - bx)
    length = 7
    spread = 0.55
    points = [
        (px, py),
        (px - length * math.cos(angle - spread), py - length * math.sin(angle - spread)),
        (px - length * math.cos(angle + spread), py - length * math.sin(angle + spread)),
    ]
    path = c.beginPath()
    path.moveTo(*points[0])
    path.lineTo(*points[1])
    path.lineTo(*points[2])
    path.close()
    c.drawPath(path, fill=1, stroke=0)
    if label:
        lx = (x1 + x2) / 2 if bend is None else bend[0]
        ly = (y1 + y2) / 2 if bend is None else bend[1]
        c.setFont(FONT, 7)
        c.setFillColor(COLORS["muted"])
        c.drawCentredString(lx, ly + 4, label)


def down(c, a, b, label=""):
    arrow(c, (a[0] + a[2] / 2, a[1]), (b[0] + b[2] / 2, b[1] + b[3]), label)


def right(c, a, b, label=""):
    arrow(c, (a[0] + a[2], a[1] + a[3] / 2), (b[0], b[1] + b[3] / 2), label)


def left(c, a, b, label=""):
    arrow(c, (a[0], a[1] + a[3] / 2), (b[0] + b[2], b[1] + b[3] / 2), label)


def page_title(c, title, subtitle, page_no):
    c.setFillColor(COLORS["dark"])
    c.rect(0, PAGE_H - 58, PAGE_W, 58, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont(FONT, 20)
    c.drawString(28, PAGE_H - 34, title)
    c.setFont(FONT, 9)
    c.drawRightString(PAGE_W - 28, PAGE_H - 32, subtitle)
    c.setFillColor(COLORS["muted"])
    c.setFont(FONT, 8)
    c.drawRightString(PAGE_W - 24, 18, f"第 {page_no} 页")


def lane(c, x, y, w, h, title):
    c.setStrokeColor(colors.HexColor("#CBD5E1"))
    c.setFillColor(colors.HexColor("#F8FAFC"))
    c.roundRect(x, y, w, h, 8, fill=1, stroke=1)
    c.setFillColor(COLORS["dark"])
    c.roundRect(x, y + h - 30, w, 30, 8, fill=1, stroke=0)
    c.rect(x, y + h - 30, w, 8, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont(FONT, 11)
    c.drawCentredString(x + w / 2, y + h - 20, title)


def overview_page(c):
    page_title(c, "TX - RX - STM 总体执行链路", "先建立全局记忆，再分别看三端主循环", 1)
    lane(c, 25, 72, PAGE_W - 50, PAGE_H - 150, "图片主链路")
    y = PAGE_H - 235
    w, h, gap = 178, 78, 42
    x0 = 45
    nodes = [
        box(c, x0, y, w, h, "TX采集\nOV7676 → CX93510 JPEG\n同步读取ADXL362", "state", 9),
        box(c, x0 + (w + gap), y, w, h, "TX无线发送\nBEGIN×2 → DATA分片 → END\n首轮END后等待ACK", "process", 9),
        box(c, x0 + 2 * (w + gap), y, w, h, "RX Radio中断\nCRC/RSSI分类\n扫描包消费或业务包入队", "event", 9),
        box(c, x0 + 3 * (w + gap), y, w, h, "RX图片模块\n校验BEGIN → 去重DATA\nEND检查完整性和校验和", "process", 9),
        box(c, x0 + 4 * (w + gap), y, w, h, "STM USART2 DMA\n解析FF 55 12 34 81\n补全DeviceInfo/JPEG", "state", 9),
    ]
    for i in range(len(nodes) - 1):
        right(c, nodes[i], nodes[i + 1], "2.4GHz" if i == 1 else ("UART 1Mbps" if i == 3 else ""))

    feedback = box(c, 350, y - 145, 320, 78, "RX → TX反馈\n0x10：图片接收成功\n0x12：暂停图片并进入快速SN广播", "event", 9)
    arrow(c, (nodes[3][0] + nodes[3][2] / 2, nodes[3][1]), (feedback[0] + feedback[2], feedback[1] + feedback[3] / 2), "生成反馈")
    arrow(c, (feedback[0], feedback[1] + feedback[3] / 2), (nodes[1][0] + nodes[1][2] / 2, nodes[1][1]), "Radio返回")

    sd = box(c, 770, y - 145, 175, 70, "SD卡\n追加Y<SN>.YS\n固定长度记录", "process", 9)
    pc = box(c, 975, y - 145, 175, 70, "PC / U盘\nUSART3 DMA\nZAYS 0x81图片", "process", 9)
    arrow(c, (nodes[4][0] + nodes[4][2] / 2, nodes[4][1]), (sd[0] + sd[2] / 2, sd[1] + sd[3]), "FatFs")
    arrow(c, (nodes[4][0] + nodes[4][2] / 2, nodes[4][1]), (pc[0] + pc[2] / 2, pc[1] + pc[3]), "USART3")

    lane(c, 45, 105, 690, 190, "控制与绑定链路")
    control_nodes = [
        box(c, 70, 155, 135, 62, "PC控制命令\nZAYS帧", "event", 9),
        box(c, 240, 145, 150, 82, "STM\n0x02本地设置RTC\n其他命令转发RX", "process", 8),
        box(c, 430, 140, 165, 92, "RX\n查询/绑定/解绑本地处理\n0x40～0x46转发TX", "process", 8),
        box(c, 635, 140, 170, 92, "TX配置窗口\n执行0x40～0x46\n应答无线重复3次", "process", 8),
    ]
    for i in range(len(control_nodes) - 1):
        right(c, control_nodes[i], control_nodes[i + 1])
    arrow(c, (control_nodes[3][0] + control_nodes[3][2] / 2, control_nodes[3][1]), (control_nodes[0][0] + control_nodes[0][2] / 2, control_nodes[0][1]), "应答沿原路返回", bend=(850, 120))

    note = box(c, 850, 112, 300, 165,
               "记忆主线\n\n1. TX只负责采集、分片和等待反馈。\n2. RX先选天线，再重组图片，再转UART。\n3. STM完成最终校验、设备信息、存储和PC转发。\n4. 中断只搬数据或置标志，业务尽量留在主循环。",
               "note", 9)


def tx_page(c):
    page_title(c, "TX 发送端代码执行流程", "入口：nrf_tx/.../TX/main.c", 2)
    margin, gap = 22, 16
    lane_w = (PAGE_W - 2 * margin - 2 * gap) / 3
    lane_y, lane_h = 48, PAGE_H - 125
    xs = [margin, margin + lane_w + gap, margin + 2 * (lane_w + gap)]
    lane(c, xs[0], lane_y, lane_w, lane_h, "A. 上电与SN配置")
    lane(c, xs[1], lane_y, lane_w, lane_h, "B. application_run()固定顺序")
    lane(c, xs[2], lane_y, lane_w, lane_h, "C. 拍照、分片与ACK")

    x = xs[0] + 22
    bw = lane_w - 44
    items = [
        ("main()", "start", 38),
        ("application_init()\n时钟 → 日志 → SN → 看门狗 → LED\nCX93510/OV7676 → SPIM挂起 → ADXL362\nRadio → TIMER1", "process", 82),
        ("sn_config_window_run()\n打开0x40～0x46窗口", "process", 52),
        ("配置完成或连续无合法命令超时？", "decision", 48),
        ("否：radio_rx_process()\n翻转LED → 日志 → 喂狗 → WFE\n然后再次判断", "event", 65),
        ("是：关闭配置窗口和LED", "process", 42),
        ("radio_enter_idle()", "process", 38),
        ("当前SN连续广播3次", "process", 40),
        ("启动RTC2低功耗调度器", "process", 42),
        ("g_capture_due=true\n首帧立即采集", "state", 46),
    ]
    y = PAGE_H - 126
    prev = None
    startup_boxes = []
    for text, kind, h in items:
        y -= h
        b = box(c, x, y, bw, h - 5, text, kind, 7.5)
        startup_boxes.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 7
    arrow(c, (startup_boxes[4][0], startup_boxes[4][1] + startup_boxes[4][3] / 2),
          (startup_boxes[3][0], startup_boxes[3][1] + startup_boxes[3][3] / 2), "循环",
          bend=(startup_boxes[3][0] - 12, startup_boxes[4][1] + startup_boxes[4][3] / 2))

    x = xs[1] + 22
    y = PAGE_H - 126
    loop_items = [
        ("进入一轮application_run()", "start", 42),
        ("image_runtime_busy 且活动时钟关闭？", "decision", 46),
        ("是：active_clock_start()", "process", 38),
        ("1. radio_rx_process()", "process", 38),
        ("2. image_ack_service()", "process", 38),
        ("3. image_fast_scan_service()", "process", 38),
        ("4. capsule_sn_broadcast_service()", "process", 42),
        ("5. image_capture_task()", "process", 38),
        ("6. image_tx_service()\n每轮最多发送一个DATA", "process", 48),
        ("7. TX_LOG_PROCESS()", "process", 36),
        ("8. watchdog_feed()", "process", 36),
        ("活动时钟开启且当前无任务？", "decision", 44),
        ("是：active_clock_stop()", "process", 38),
        ("9. __WFE()等待中断", "event", 40),
    ]
    prev = None
    loop_boxes = []
    for text, kind, h in loop_items:
        y -= h
        b = box(c, x, y, bw, h - 4, text, kind, 7.3)
        loop_boxes.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 4
    arrow(c, (loop_boxes[-1][0], loop_boxes[-1][1] + loop_boxes[-1][3] / 2),
          (loop_boxes[0][0], loop_boxes[0][1] + loop_boxes[0][3] / 2), "下一轮",
          bend=(loop_boxes[-1][0] - 15, loop_boxes[-1][1] + loop_boxes[-1][3] / 2))

    x = xs[2] + 22
    y = PAGE_H - 126
    image_items = [
        ("capture_due到期\n唤醒摄像头、开灯、启动ADXL362", "process", 58),
        ("等待25ms预热截止", "event", 40),
        ("读取加速度 → 抓取JPEG\n关灯 → OV7676休眠", "process", 52),
        ("JPEG长度是否合法？", "decision", 42),
        ("设置帧ID、分片数和校验初值\nactive=true", "state", 52),
        ("首轮第0片：BEGIN发送2次", "process", 42),
        ("每轮读取一片JPEG\n累加校验和 → 发送DATA", "process", 52),
        ("所有分片完成？", "decision", 40),
        ("发送END，首轮打开RX等待ACK", "process", 46),
        ("收到匹配反馈？", "decision", 40),
        ("0x10：完成并关闭Radio RX", "state", 40),
        ("0x12：快速扫描\n每8ms发0x05，前120ms插入0x11", "event", 58),
        ("ACK超时：重发DATA+END\n不重发BEGIN；达到上限则放弃", "error", 58),
    ]
    prev = None
    image_boxes = []
    for text, kind, h in image_items:
        h *= 0.80
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 6.9)
        image_boxes.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (image_boxes[7][0], image_boxes[7][1] + image_boxes[7][3] / 2),
          (image_boxes[6][0], image_boxes[6][1] + image_boxes[6][3] / 2), "否：下一片",
          bend=(image_boxes[6][0] - 12, image_boxes[7][1] + image_boxes[7][3] / 2))
    box(c, xs[2] + 28, 58, lane_w - 56, 56,
        "中断只置事件：TIMER1更新时间；RTC2置广播/采集标志；Radio中断把CRC正确包写入队列。",
        "note", 7.2)


def rx_page(c):
    page_title(c, "RX 接收端代码执行流程", "入口：nrf_rx/.../receiver/main.c", 3)
    margin, gap = 22, 16
    lane_w = (PAGE_W - 2 * margin - 2 * gap) / 3
    lane_y, lane_h = 48, PAGE_H - 125
    xs = [margin, margin + lane_w + gap, margin + 2 * (lane_w + gap)]
    lane(c, xs[0], lane_y, lane_w, lane_h, "A. 初始化与run_once()")
    lane(c, xs[1], lane_y, lane_w, lane_h, "B. Radio包与图片重组")
    lane(c, xs[2], lane_y, lane_w, lane_h, "C. 天线状态机与UART")

    x = xs[0] + 20
    bw = lane_w - 40
    items = [
        ("receiver_platform_init()\n时钟 → 模式脚 → RF1662默认天线", "process", 54),
        ("receiver_service_init()\n日志 → 绑定 → 图片 → TIMER1", "process", 50),
        ("receiver_comm_init()\nUART空闲捕获 → UART → Radio → 天线管理", "process", 58),
        ("进入while(true)", "start", 38),
        ("1. antenna_service_events()", "process", 36),
        ("2. radio_process_one()", "process", 36),
        ("3. uart_rx_service()", "process", 36),
        ("4. uart_control_service()", "process", 36),
        ("5. uart_tx_service()", "process", 36),
        ("6. antenna_service_events()\n处理本轮刚产生的END/0x11", "process", 48),
        ("7. antenna_service_schedule()", "process", 38),
        ("NRF_LOG_PROCESS()", "process", 34),
        ("本轮无业务且无日志？", "decision", 42),
        ("是：__WFE()等待中断", "event", 40),
    ]
    y = PAGE_H - 126
    boxes = []
    prev = None
    for text, kind, h in items:
        h *= 0.82
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 7.0)
        boxes.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (boxes[-1][0], boxes[-1][1] + boxes[-1][3] / 2),
          (boxes[3][0], boxes[3][1] + boxes[3][3] / 2), "下一轮",
          bend=(boxes[-1][0] - 12, boxes[-1][1] + boxes[-1][3] / 2))
    box(c, x, 57, bw, 58, "为什么事件服务调用两次：第一次处理旧事件；radio_process_one()可能新产生事件，因此调度前再处理一次。", "note", 7.2)

    x = xs[1] + 20
    y = PAGE_H - 126
    items = [
        ("RADIO_IRQHandler()\n读取CRC和RSSI", "event", 46),
        ("当前为SEEK_END或FAST_SCAN？", "decision", 44),
        ("是：天线模块直接消费\n挂起END/0x11或累计0x05 RSSI", "state", 55),
        ("否：CRC正确且需要该包？", "decision", 42),
        ("复制到Radio环形队列", "process", 38),
        ("主循环每轮取一个包", "process", 38),
        ("control_handle_radio_packet()", "process", 40),
        ("已作为0x11 / 0x05 / ZAYS处理？", "decision", 44),
        ("否：image_process_packet()", "process", 40),
        ("BEGIN：校验长度/分片数\n建立重组上下文", "process", 50),
        ("DATA：按索引去重复制\n最后一片只复制JPEG剩余长度", "process", 54),
        ("END：分片完整且整图校验正确？", "decision", 48),
        ("否：不完整/校验失败\n记录失败，必要时触发换天线", "error", 52),
        ("是：优先0x12，否则0x10 ACK\n构建DeviceInfo并queue_image()", "state", 58),
    ]
    boxes2 = []
    prev = None
    for text, kind, h in items:
        h *= 0.84
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 6.9)
        boxes2.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (boxes2[11][0], boxes2[11][1] + boxes2[11][3] / 2),
          (boxes2[12][0] + boxes2[12][2], boxes2[12][1] + boxes2[12][3] / 2), "否")

    x = xs[2] + 20
    y = PAGE_H - 126
    items = [
        ("DISCOVERY\n未绑定，按驻留时间轮询12路", "state", 52),
        ("绑定成功？", "decision", 38),
        ("SEEK_END\n8ms逐路寻找绑定胶囊END", "state", 48),
        ("捕获END：最多发两次0x12\n当前天线保持150ms", "process", 54),
        ("收到匹配0x11？", "decision", 40),
        ("FAST_SCAN\n逐路累计有效0x05数量与RSSI", "state", 52),
        ("扫描结束，构建最多3路候选", "process", 42),
        ("存在有效候选？", "decision", 40),
        ("LOCKED：锁定第一候选", "state", 42),
        ("目标包超时 / 完整图超时\n或连续失败达到限制？", "decision", 54),
        ("有下一候选：切换候选\n候选耗尽：重新SEEK_END", "error", 54),
        ("UART图片队列\nFF55123481 + DeviceInfo + JPEG + checksum", "process", 58),
        ("uart_tx_service()每轮启动一个DMA块", "process", 44),
        ("TX_EMPTY中断只置完成标志\n主循环决定下一块或释放整帧", "event", 54),
    ]
    boxes3 = []
    prev = None
    for text, kind, h in items:
        h *= 0.78
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 6.8)
        boxes3.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (boxes3[10][0], boxes3[10][1] + boxes3[10][3] / 2),
          (boxes3[2][0] + boxes3[2][2], boxes3[2][1] + boxes3[2][3] / 2), "候选耗尽",
          bend=(boxes3[2][0] + boxes3[2][2] + 10, boxes3[10][1] + boxes3[10][3] / 2))


def stm_page(c):
    page_title(c, "STM32 主控代码执行流程", "入口：stm32F103RET6/stm_project/Core/Src/main.c", 4)
    margin, gap = 22, 16
    lane_w = (PAGE_W - 2 * margin - 2 * gap) / 3
    lane_y, lane_h = 48, PAGE_H - 125
    xs = [margin, margin + lane_w + gap, margin + 2 * (lane_w + gap)]
    lane(c, xs[0], lane_y, lane_w, lane_h, "A. 初始化与主循环")
    lane(c, xs[1], lane_y, lane_w, lane_h, "B. USART2 DMA与图片解析")
    lane(c, xs[2], lane_y, lane_w, lane_h, "C. 存储、PC转发与控制")

    x = xs[0] + 20
    bw = lane_w - 40
    items = [
        ("HAL_Init()", "start", 36),
        ("SystemClock_Config()\nHSE 8MHz × PLL9 = 72MHz", "process", 48),
        ("按顺序初始化\nGPIO → DMA → IWDG → TIM1\nUSART1 → USART2 → USART3 → SDIO → FATFS", "process", 72),
        ("AppLog_Init()", "process", 36),
        ("AppUartRx_Init()\n协议初始化 + USART2/3循环DMA", "process", 50),
        ("初始化成功？", "decision", 38),
        ("否：Error_Handler()\n关中断，等待IWDG复位", "error", 46),
        ("是：启动TIM1 1ms中断", "process", 40),
        ("SdStorage_Init()\n失败仅告警，程序继续", "process", 48),
        ("进入while(1)", "start", 36),
        ("AppUartRx_Process()", "process", 38),
        ("1. Consume USART2 ring", "process", 36),
        ("2. Consume USART3 ring", "process", 36),
        ("3. CapsuleProtocol_Process()", "process", 38),
        ("HAL_IWDG_Refresh()", "process", 36),
    ]
    y = PAGE_H - 126
    boxes = []
    prev = None
    for text, kind, h in items:
        h *= 0.82
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 7.0)
        boxes.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (boxes[-1][0], boxes[-1][1] + boxes[-1][3] / 2),
          (boxes[9][0], boxes[9][1] + boxes[9][3] / 2), "下一轮",
          bend=(boxes[-1][0] - 12, boxes[-1][1] + boxes[-1][3] / 2))

    x = xs[1] + 20
    y = PAGE_H - 126
    items = [
        ("USART2 IDLE / DMA半满 / DMA全满", "event", 42),
        ("HAL_UARTEx_RxEventCallback()\n只提交本次DMA新增字节", "event", 50),
        ("DMA发生回绕？", "decision", 38),
        ("未回绕：复制[last,current)\n已回绕：复制尾段 + 头段", "process", 52),
        ("写入USART2软件RingBuffer", "state", 40),
        ("主循环InputFromNrf()", "process", 40),
        ("PREAMBLE状态识别到ZAYS控制应答？", "decision", 48),
        ("是：校验并保存pending\n等待USART3图片DMA空闲", "process", 50),
        ("否：搜索 FF 55 12 34 81", "process", 42),
        ("读取payload长度\n必须为128～128+20000", "decision", 48),
        ("持续拼接DeviceInfo + JPEG + checksum", "process", 48),
        ("完整帧收齐？", "decision", 38),
        ("HandleFrame()\n检查长度、总校验和、DeviceInfo标识", "process", 54),
        ("全部正确？", "decision", 38),
        ("否：rejected_frames++并重置", "error", 42),
        ("是：有DQT/DHT直接复制\n否则插入legacy JPEG头", "state", 52),
    ]
    y = PAGE_H - 126
    boxes2 = []
    prev = None
    for text, kind, h in items:
        h *= 0.78
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 6.8)
        boxes2.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    arrow(c, (boxes2[11][0], boxes2[11][1] + boxes2[11][3] / 2),
          (boxes2[10][0], boxes2[10][1] + boxes2[10][3] / 2), "否：继续拼接",
          bend=(boxes2[10][0] - 12, boxes2[11][1] + boxes2[11][3] / 2))

    x = xs[2] + 20
    y = PAGE_H - 126
    items = [
        ("补充STM版本、芯片ID、运行时间、RTC和JPEG长度", "process", 54),
        ("根据胶囊SN生成 Y<SN>.YS", "process", 42),
        ("上一幅USART3图片DMA空闲？", "decision", 44),
        ("ForwardToPc()\n启动7字节ZAYS 0x81头DMA", "process", 48),
        ("TX完成回调链\nHEADER → JPEG → DeviceInfo → checksum", "event", 56),
        ("checksum完成：pc_tx_state=IDLE", "state", 42),
        ("同时AppendCapsuleRecord()\n向.YS追加固定20064字节记录", "process", 54),
        ("达到同步策略？", "decision", 38),
        ("是：f_sync()", "process", 36),
        ("completed_frames++", "state", 36),
        ("USART3接收PC命令", "event", 38),
        ("命令为0x02？", "decision", 38),
        ("是：本地设置SD2058 RTC", "process", 40),
        ("否：命令/普通字节转发USART2 → RX", "process", 46),
        ("Protocol_Process()\n图片DMA空闲后发送pending控制应答", "process", 50),
    ]
    y = PAGE_H - 126
    boxes3 = []
    prev = None
    for text, kind, h in items:
        h *= 0.80
        y -= h
        b = box(c, x, y, bw, h - 3, text, kind, 6.8)
        boxes3.append(b)
        if prev:
            down(c, prev, b)
        prev = b
        y -= 2
    box(c, xs[2] + 28, 58, lane_w - 56, 54,
        "TIM1中断只做application_milliseconds++；DMA中断只提交字节。图片解析和SD写入都在主循环。",
        "note", 7.2)


def build_pdf():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUTPUT), pagesize=landscape(A3), pageCompression=1)
    c.setTitle("TX RX STM 代码流程学习版")
    c.setAuthor("Codex")
    overview_page(c)
    c.showPage()
    tx_page(c)
    c.showPage()
    rx_page(c)
    c.showPage()
    stm_page(c)
    c.showPage()
    c.save()
    print(OUTPUT)


if __name__ == "__main__":
    build_pdf()
