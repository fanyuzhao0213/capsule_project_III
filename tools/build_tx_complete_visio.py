from pathlib import Path
import re
import tempfile
import zipfile
from xml.etree import ElementTree as ET

import build_visio_package as vis
from build_visio_package import Edge, Header, Node, PageModel


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "doc" / "流程图" / "磁控胶囊流程图 - 副本.vsdm"


def n(ident, text, x, y, w=4.8, h=0.78, kind="process"):
    return Node(ident, text, x, y, w, h, kind)


def e(source, target, label=""):
    return Edge(source, target, label)


def page_sn_config():
    nodes = [
        n("start", "上电 → main()", 3.4, 24.1, 3.4, 0.72, "start"),
        n("init", "application_init()\n时钟→日志→加载SN→看门狗→LED→摄像头\n→ADXL362→Radio RX→TIMER1 1ms", 3.4, 22.35, 5.6, 1.55),
        n("load", "capsule_sn_init()\nFlash全0xFF：使用FICR DEVICEID\n否则：使用Flash用户SN", 3.4, 20.45, 5.3, 1.25),
        n("open", "capsule_sn_config_window_begin()\nupdate_pending=false：当前没有待确认的新SN\nconfig_complete=false：尚未成功写Flash\nwindow_active=true：允许处理0x40/42/44/46\nlast_activity=now：3秒静默计时起点", 3.4, 18.55, 5.7, 1.75, "state"),
        n("complete1", "config_complete？", 9.8, 23.3, 3.6, 0.72, "decision"),
        n("rx", "radio_rx_process()\n每轮最多处理一个接收包", 9.8, 21.75, 4.9, 0.9),
        n("complete2", "处理后config_complete？", 9.8, 20.15, 4.2, 0.72, "decision"),
        n("timeout", "g_time_ms-last_activity_ms ≥ 3000ms？\n连续3秒无格式合格配置命令", 9.8, 18.45, 5.5, 1.0, "decision"),
        n("led", "距离上次LED翻转 ≥100ms？\n是：翻转配置灯并更新时间", 9.8, 16.75, 5.1, 0.95, "decision"),
        n("wait", "TX_LOG_PROCESS() → watchdog_feed() → __WFE()\n中断唤醒后回到config_complete判断", 9.8, 15.1, 5.8, 0.95, "event"),
        n("close", "capsule_sn_config_window_end()\nwindow_active=false：拒绝后续配置命令\nupdate_pending=false：丢弃未确认的新SN；LED关闭", 9.8, 12.9, 5.7, 1.2, "state"),
        n("idle", "radio_enter_idle()", 9.8, 11.25, 3.7, 0.72),
        n("burst", "广播当前生效SN三次\n成功：新SN；超时/失败：原SN", 9.8, 9.65, 5.0, 0.95),
        n("rtc", "启动RTC2低功耗调度\ng_capture_due=true → 首帧立即采集", 9.8, 8.0, 5.2, 0.95, "state"),
        n("frame", "Radio包先通过硬件CRC并进入环形队列\nZAYS：5A 41 59 53 + cmd + len + payload + checksum", 16.5, 23.0, 5.7, 1.15, "note"),
        n("valid", "ZAYS长度和payload校验正确？", 16.5, 21.1, 4.7, 0.72, "decision"),
        n("refresh", "仅下列命令号+长度刷新last_activity：\n0x40/0；0x42/0；0x44/16；0x46/0", 16.5, 19.4, 5.4, 1.05, "state"),
        n("cmd40", "0x40：清除未确认pending SN\n返回0x41×3", 16.5, 17.65, 4.8, 0.92),
        n("cmd42", "0x42：读取8字节FICR DEVICEID\n返回0x43+DEVICEID，发送3次", 16.5, 15.95, 5.1, 0.95),
        n("cmd44", "0x44：payload=DEVICEID+新SN\nID匹配→SN暂存RAM，返回0x45×3\n不匹配→0x49×3", 16.5, 13.95, 5.5, 1.35),
        n("cmd46", "0x46：存在pending？\n写Flash并回读校验；成功→complete=true、0x47×3\n失败/无pending→0x49×3", 16.5, 11.55, 5.7, 1.55, "decision"),
        n("bad", "非ZAYS/CRC错误：不入控制流程\nZAYS长度或校验错误：消费但不刷新计时\n未知命令/错误长度：0x49，不刷新计时", 16.5, 8.9, 5.8, 1.45, "error"),
    ]
    edges = [
        e("start", "init"), e("init", "load"), e("load", "open"), e("open", "complete1"),
        e("complete1", "close", "是"), e("complete1", "rx", "否"), e("rx", "complete2"),
        e("complete2", "close", "是"), e("complete2", "timeout", "否"),
        e("timeout", "close", "是：超时"), e("timeout", "led", "否"), e("led", "wait"),
        e("close", "idle"), e("idle", "burst"), e("burst", "rtc"),
        e("frame", "valid"), e("valid", "refresh", "是"), e("valid", "bad", "否"),
        e("refresh", "cmd40", "0x40"), e("refresh", "cmd42", "0x42"),
        e("refresh", "cmd44", "0x44"), e("refresh", "cmd46", "0x46"),
    ]
    headers = [Header("上电与有效SN", 3.4, 25.05, 5.7), Header("3秒无活动配置窗口", 9.8, 25.05, 5.8), Header("ZAYS配置命令处理", 16.5, 25.05, 5.8)]
    return PageModel("tx_sn", "TX-01 上电与SN配置", 20, 26, nodes, headers, edges)


def page_application_run():
    nodes = [
        n("entry", "进入application_run()", 7.3, 21.7, 4.3, 0.75, "start"),
        n("flag", "active_clock_running=true\n主循环对HFXO/TIMER1运行状态的本地记录", 7.3, 20.3, 5.2, 0.9, "state"),
        n("loop", "while(true)", 7.3, 18.95, 3.2, 0.68, "state"),
        n("wakeq", "image_runtime_busy() && !active_clock_running？", 7.3, 17.45, 5.8, 0.78, "decision"),
        n("clockon", "active_clock_start()\n启动HFXO和TIMER1；标志=true", 7.3, 15.95, 5.3, 0.88),
        n("radio", "1  radio_rx_process()", 7.3, 14.55, 4.6, 0.68),
        n("ack", "2  image_ack_service()", 7.3, 13.35, 4.6, 0.68),
        n("scan", "3  image_fast_scan_service()", 7.3, 12.15, 4.9, 0.68),
        n("sn", "4  capsule_sn_broadcast_service()", 7.3, 10.95, 5.3, 0.68),
        n("capture", "5  image_capture_task()", 7.3, 9.75, 4.7, 0.68),
        n("tx", "6  image_tx_service()\n每轮最多发送一个DATA分片", 7.3, 8.45, 5.1, 0.82),
        n("house", "7  TX_LOG_PROCESS() → watchdog_feed()", 7.3, 7.05, 5.4, 0.72),
        n("sleepq", "active_clock_running && !image_runtime_busy()？", 7.3, 5.55, 5.8, 0.78, "decision"),
        n("clockoff", "active_clock_stop()\n停止TIMER1、释放HFXO；标志=false", 7.3, 4.0, 5.4, 0.9),
        n("wfe", "__WFE() 等待中断\n唤醒后回到while顶部", 7.3, 2.5, 4.1, 0.9, "event"),
        n("busy", "image_runtime_busy()为true的6种情况：\n①快速扫描 ②普通SN待发 ③采集请求\n④预热中 ⑤图片发送中 ⑥等待ACK", 15.5, 18.0, 6.0, 1.55, "note"),
        n("irq", "唤醒来源：\nRTC2→设置采集/SN请求\nRADIO→CRC正确包入队\nTIMER1→g_time_ms++（仅活动阶段）", 15.5, 14.8, 5.8, 1.55, "note"),
        n("order", "顺序要点：先处理Radio，再检查ACK超时\n边界时刻到达的ACK可先清awaiting_ack，避免误重发", 15.5, 11.8, 6.0, 1.25, "note"),
        n("compile", "image_capture_task()/image_tx_service()\n只在IMAGE_TRANSMISSION_ENABLED开启时编译", 15.5, 9.25, 5.8, 1.15, "note"),
    ]
    edges = [e("entry", "flag"), e("flag", "loop"), e("loop", "wakeq"), e("wakeq", "clockon", "是"), e("wakeq", "radio", "否"), e("clockon", "radio"), e("radio", "ack"), e("ack", "scan"), e("scan", "sn"), e("sn", "capture"), e("capture", "tx"), e("tx", "house"), e("house", "sleepq"), e("sleepq", "clockoff", "是"), e("sleepq", "wfe", "否"), e("clockoff", "wfe")]
    headers = [Header("主循环骨架", 7.3, 22.75, 7.2), Header("状态和中断说明", 15.5, 22.75, 6.4)]
    return PageModel("tx_loop", "TX-02 application_run低功耗调度", 20, 24, nodes, headers, edges)


def page_radio_rx():
    nodes = [
        n("irq", "RADIO_IRQHandler()\nEND事件且CRCSTATUS正确", 3.4, 21.0, 5.0, 0.95, "event"),
        n("space", "环形队列有空间？", 3.4, 19.4, 3.8, 0.72, "decision"),
        n("enqueue", "复制Radio包到tail\ntail前移；重新arm RX", 3.4, 17.85, 4.6, 0.9),
        n("drop", "队列满：dropped++\n仍重新arm RX", 3.4, 16.25, 4.3, 0.9, "error"),
        n("entry", "radio_rx_process()", 9.9, 21.0, 4.3, 0.75, "start"),
        n("empty", "head != tail？", 9.9, 19.55, 3.5, 0.72, "decision"),
        n("read", "保存head；packet=data[head]", 9.9, 18.15, 4.8, 0.72),
        n("log", "配置窗口打开？\n是：打印packet[0..3]", 9.9, 16.75, 4.3, 0.85, "decision"),
        n("ctrl", "radio_process_control_frame(packet)返回true？", 9.9, 15.2, 5.7, 0.78, "decision"),
        n("handled", "ZAYS控制帧已消费\n不再进入ACK判断", 9.9, 13.7, 4.6, 0.85, "state"),
        n("match", "同时满足？\nawaiting_ack=true：END已发，RX窗口正在等待回应\n命令=0x10或0x12\nFrame ID匹配；8字节SN匹配", 16.5, 18.7, 5.8, 1.7, "decision"),
        n("ignore", "任一条件不满足：忽略内容", 16.5, 16.25, 4.8, 0.75, "note"),
        n("type", "packet[0]==0x12？", 16.5, 14.7, 3.9, 0.72, "decision"),
        n("common", "共同处理：\nawaiting_ack=false；retry_count=0\nradio_enter_idle()；CX93510 host suspend", 16.5, 12.75, 5.8, 1.25, "state"),
        n("scan", "0x12额外处理：\nfast_scan_active=true：暂停图片并进入密集SN广播\n记录开始/下一发送时间；计数清零\n清capture_due和sn_broadcast_due", 16.5, 10.4, 6.0, 1.55),
        n("dequeue", "无论有效/无效：\nhead=(head+1)%RADIO_QUEUE_DEPTH\n当前包出队", 9.9, 9.0, 5.3, 1.15, "state"),
        n("return", "返回；每次调用最多处理一包", 9.9, 7.35, 4.8, 0.75, "start"),
    ]
    edges = [e("irq", "space"), e("space", "enqueue", "是"), e("space", "drop", "否"), e("entry", "empty"), e("empty", "return", "否"), e("empty", "read", "是"), e("read", "log"), e("log", "ctrl"), e("ctrl", "handled", "是"), e("ctrl", "match", "否"), e("handled", "dequeue"), e("match", "ignore", "否"), e("match", "type", "是"), e("ignore", "dequeue"), e("type", "common", "0x10"), e("type", "common", "0x12"), e("common", "scan", "仅0x12"), e("common", "dequeue", "0x10"), e("scan", "dequeue"), e("dequeue", "return")]
    headers = [Header("Radio中断入队", 3.4, 22.25, 5.7), Header("主循环取包与控制帧优先级", 9.9, 22.25, 6.0), Header("0x10 ACK / 0x12扫描请求", 16.5, 22.25, 5.8)]
    return PageModel("tx_radio", "TX-03 Radio接收与ACK匹配", 20, 23.5, nodes, headers, edges)


def page_ack_retry():
    nodes = [
        n("entry", "image_ack_service()", 7.0, 19.7, 4.2, 0.75, "start"),
        n("wait", "awaiting_ack？", 7.0, 18.2, 3.5, 0.72, "decision"),
        n("deadline", "(int32_t)(g_time_ms-ack_deadline_ms) ≥ 0？", 7.0, 16.65, 5.8, 0.78, "decision"),
        n("log", "记录ACK超时日志", 7.0, 15.2, 4.0, 0.68),
        n("clear", "awaiting_ack=false\nradio_enter_idle()关闭RX窗口", 7.0, 13.75, 4.8, 0.9, "state"),
        n("limit", "retry_count ≥ IMAGE_MAX_RETRIES？", 7.0, 12.2, 5.2, 0.75, "decision"),
        n("abandon", "达到上限：retry_count=0\nCX93510 host suspend；放弃本帧", 3.6, 10.45, 5.0, 1.0, "error"),
        n("retry", "retry_count++；checksum=0\nselect_image_block(false)：JPEG游标/分片归零", 10.4, 10.45, 5.8, 1.1),
        n("nobegin", "legacy_send_begin=false：重发时跳过BEGIN\nnext_fragment_ms=当前时间\nactive=true：重新启动DATA/END发送状态机", 10.4, 8.55, 5.6, 1.15, "state"),
        n("datas", "后续image_tx_service()\n直接重发全部DATA（不发BEGIN）→ 重发END", 10.4, 6.7, 5.7, 1.0),
        n("end", "重发END后不再打开RX、不等第二次ACK\nawaiting_ack=false；retry_count=0；SPIM休眠", 10.4, 4.7, 5.9, 1.1, "start"),
        n("return0", "否：未等待ACK或尚未超时\n直接返回主循环", 3.3, 15.8, 4.8, 0.9, "note"),
        n("note", "当前参数：ACK_TIMEOUT=30ms，MAX_RETRIES=1\n真实行为：首轮等一次ACK；超时后单向补发DATA+END一次。\n若只修改MAX_RETRIES>1，现代码也不会自动多次重试。", 15.8, 15.5, 6.0, 1.7, "note"),
        n("boundary", "application_run中先radio_rx_process()再ack_service()\n边界到达的ACK会先清awaiting_ack，不会误重发", 15.8, 12.6, 5.8, 1.2, "note"),
    ]
    edges = [e("entry", "wait"), e("wait", "deadline", "是"), e("wait", "return0", "否"), e("deadline", "return0", "否"), e("deadline", "log", "是：超时"), e("log", "clear"), e("clear", "limit"), e("limit", "abandon", "是"), e("limit", "retry", "否"), e("retry", "nobegin"), e("nobegin", "datas"), e("datas", "end")]
    headers = [Header("ACK超时状态转换", 7.0, 20.8, 7.0), Header("当前协议的重要说明", 15.8, 20.8, 6.2)]
    return PageModel("tx_ack", "TX-04 ACK超时与DATA+END重发", 19, 22, nodes, headers, edges)


def page_capture():
    nodes = [
        n("entry", "image_capture_task()", 7.0, 21.5, 4.2, 0.75, "start"),
        n("scan", "fast_scan_active？", 7.0, 20.0, 3.8, 0.72, "decision"),
        n("drop", "清g_capture_due：丢弃扫描期间的采集请求\n然后返回", 3.4, 18.45, 4.6, 0.9, "note"),
        n("warm", "capture_prep_state==WARMUP？\n表示灯、摄像头和加速度计正在等25ms稳定", 7.0, 18.45, 5.4, 1.0, "decision"),
        n("due", "g_capture_due？\nRTC2每个图片周期置true\n主循环开始处理时清false；否则直接返回", 10.6, 16.9, 4.8, 1.15, "decision"),
        n("consume", "g_capture_due=false\n上一帧active或awaiting_ack？\n是：跳过本周期并返回", 10.6, 15.35, 5.0, 1.12, "decision"),
        n("prepare", "恢复CX93510 SPIM→开补光灯\n启动ADXL362测量→唤醒OV7676", 10.6, 13.55, 5.1, 1.0),
        n("wakefail", "唤醒失败：加速度待机、关灯、摄像头休眠\nSPIM休眠并返回", 15.7, 13.55, 5.5, 1.0, "error"),
        n("deadline", "warmup_deadline=now+25ms\nprep_state=WARMUP；返回主循环", 10.6, 11.65, 5.0, 0.95, "state"),
        n("wait", "到达warmup_deadline？\n未到：直接返回", 3.4, 16.9, 4.2, 0.82, "decision"),
        n("sample", "读取加速度并令ADXL362待机\n触发CX93510抓取一帧JPEG", 3.4, 15.25, 5.1, 0.95),
        n("sleep", "关补光灯→OV7676休眠\nprep_state=IDLE", 3.4, 13.55, 4.4, 0.9),
        n("ok", "抓图成功且JPEG长度合法？", 3.4, 11.85, 4.7, 0.75, "decision"),
        n("reject", "失败/空帧/超上限：\nSPIM休眠，放弃本周期", 3.4, 10.1, 4.4, 0.95, "error"),
        n("setup", "frame_id递增；checksum=0；retry_count=0\nlegacy_send_begin=true：首轮第0片前发送BEGIN\n选择JPEG块", 7.0, 8.35, 5.9, 1.25, "state"),
        n("active", "next_fragment_ms=now：首片可立即发送\nactive=true：通知image_tx_service处理本帧", 7.0, 6.55, 5.6, 1.05, "start"),
    ]
    edges = [e("entry", "scan"), e("scan", "drop", "是"), e("scan", "warm", "否"), e("warm", "wait", "是"), e("warm", "due", "否"), e("wait", "sample", "到期"), e("sample", "sleep"), e("sleep", "ok"), e("ok", "reject", "否"), e("ok", "setup", "是"), e("due", "consume", "是"), e("consume", "prepare", "可采集"), e("prepare", "wakefail", "唤醒失败"), e("prepare", "deadline", "成功"), e("setup", "active")]
    headers = [Header("非阻塞采集状态机", 7.0, 22.6, 8.0), Header("失败路径", 15.7, 22.6, 5.2)]
    return PageModel("tx_capture", "TX-05 图片采集与25ms预热", 19, 24, nodes, headers, edges)


def page_image_tx():
    nodes = [
        n("entry", "image_tx_service()", 7.2, 22.3, 4.1, 0.72, "start"),
        n("ready", "active=true：当前仍有BEGIN/DATA/END待发送？\n且now≥next_fragment_ms：已到下一片发送时刻？\n否：直接返回", 7.2, 20.85, 5.8, 1.15, "decision"),
        n("remain", "计算remaining和本片payload_length\n最后一片只取JPEG剩余字节", 7.2, 19.3, 5.2, 0.95),
        n("beginq", "第0片、第0遍且legacy_send_begin=true？\ntrue仅表示首轮需要BEGIN；ACK超时重发时为false", 7.2, 17.7, 5.9, 1.0, "decision"),
        n("begin", "组装BEGIN：Frame ID、SN、JPEG长度、分片数\n版本和加速度；连续发送2次", 12.9, 17.7, 5.7, 1.1),
        n("read", "组装DATA头；从CX93510帧缓冲读取本片JPEG", 7.2, 15.9, 5.7, 0.9),
        n("readok", "读取成功？", 7.2, 14.4, 3.4, 0.72, "decision"),
        n("fail", "失败：active=false；awaiting_ack=false\n计数清零、关灯、SPIM休眠", 12.9, 14.4, 5.4, 1.0, "error"),
        n("send", "累加本片有效字节checksum→发送DATA\nblock_sent增加；fragment_index++", 7.2, 12.7, 5.4, 1.0),
        n("gap", "next_fragment_ms=now+分片间隔", 7.2, 11.15, 4.6, 0.72, "state"),
        n("done", "block_sent==block_size？\n否：返回，下一轮继续", 7.2, 9.7, 4.4, 0.82, "decision"),
        n("morepass", "还需重复发送本块？", 12.9, 9.7, 4.2, 0.72, "decision"),
        n("resetpass", "分片号和已发字节清零\n等待pass间隔；返回主循环", 12.9, 8.15, 4.9, 0.9),
        n("end", "组装END：Frame ID、SN、整图checksum\n发送END；active=false", 7.2, 7.9, 5.1, 1.0),
        n("first", "retry_count==0？", 7.2, 6.3, 3.8, 0.72, "decision"),
        n("waitack", "首轮：发送END后恢复RX\nawaiting_ack=true：允许0x10/0x12匹配当前帧\nack_deadline=now+30ms", 3.6, 4.55, 5.4, 1.2, "state"),
        n("retryend", "重发轮：不恢复RX、不等第二次ACK\nawaiting_ack=false；retry_count=0；SPIM休眠", 10.8, 4.55, 5.8, 1.0, "state"),
    ]
    edges = [e("entry", "ready"), e("ready", "remain", "是"), e("remain", "beginq"), e("beginq", "begin", "是"), e("beginq", "read", "否"), e("begin", "read"), e("read", "readok"), e("readok", "fail", "否"), e("readok", "send", "是"), e("send", "gap"), e("gap", "done"), e("done", "morepass", "是"), e("morepass", "resetpass", "是"), e("morepass", "end", "否"), e("end", "first"), e("first", "waitack", "是"), e("first", "retryend", "否")]
    headers = [Header("BEGIN / DATA / END发送状态机", 7.2, 23.35, 8.2), Header("分支与失败处理", 12.9, 23.35, 5.7)]
    return PageModel("tx_send", "TX-06 图片分片发送状态机", 19, 25, nodes, headers, edges)


def page_scan_sn():
    nodes = [
        n("normal", "capsule_sn_broadcast_service()", 4.5, 19.8, 5.2, 0.75, "start"),
        n("normalq", "fast_scan_active：正在快速扫描\nimage active：图片还在发送\nawaiting_ack：正在等待图片回应\nsn_broadcast_due：RTC2提出普通SN广播请求\n前三者任一true或due=false：直接返回", 4.5, 18.1, 6.0, 1.75, "decision"),
        n("normalsend", "清sn_broadcast_due\n发送0x05 + 当前8字节SN", 4.5, 16.25, 4.9, 0.95),
        n("request", "radio_rx_process()收到合法0x12\n必须awaiting_ack、Frame ID和SN均匹配", 13.9, 19.8, 6.0, 1.05, "event"),
        n("setup", "结束ACK等待、关闭RX和SPIM\nfast_scan_active=true；start/next=now\n计数清零；清capture_due和普通SN due", 13.9, 17.7, 6.1, 1.4, "state"),
        n("service", "image_fast_scan_service()", 13.9, 15.7, 4.8, 0.75, "start"),
        n("active", "fast_scan_active？\nRX合法0x12置true；1800ms到期清false\nfalse：直接返回", 13.9, 14.25, 5.0, 1.05, "decision"),
        n("duration", "elapsed=now-start\nelapsed ≥1800ms？", 13.9, 12.65, 4.3, 0.9, "decision"),
        n("finish", "结束扫描；清capture_due和普通SN due\n等待下个RTC2周期恢复图片", 16.1, 10.75, 4.8, 1.0, "state"),
        n("next", "now ≥ fast_scan_next_ms？\n未到：直接返回", 10.8, 10.75, 4.5, 0.82, "decision"),
        n("sync", "前120ms且广播计数%4==0？\n是：先发送0x11+SN", 10.8, 9.05, 4.8, 0.95, "decision"),
        n("fastsend", "每个周期发送0x05+SN\ncount++；next=now+8ms\n返回主循环", 10.8, 7.25, 4.8, 1.08),
        n("note", "快速扫描期间暂停新图片采集和普通SN广播\nimage_runtime_busy()持续为true，HFXO/TIMER1保持运行", 4.5, 10.8, 5.7, 1.25, "note"),
    ]
    edges = [e("normal", "normalq"), e("normalq", "normalsend", "否：可以发送"), e("request", "setup"), e("setup", "service"), e("service", "active"), e("active", "duration", "是"), e("duration", "finish", "是"), e("duration", "next", "否"), e("next", "sync", "到期"), e("sync", "fastsend", "发送/跳过0x11")]
    headers = [Header("普通SN广播", 4.5, 21.0, 6.6), Header("RX请求后的1800ms快速扫描广播", 13.9, 21.0, 7.2)]
    return PageModel("tx_scan", "TX-07 普通SN与快速扫描广播", 19, 22, nodes, headers, edges)


def page_flag_reference():
    nodes = [
        n("cfg_active", "m_sn_config_window_active\n作用：是否允许执行0x40/42/44/46配置命令\n置位：上电打开3秒配置窗口\n清除：窗口成功结束或静默超时", 3.5, 19.2, 5.8, 1.45, "state"),
        n("cfg_pending", "m_sn_update_pending\n作用：RAM里是否已有等待0x46确认的新SN\n置位：0x44的DEVICEID匹配并暂存新SN\n清除：0x40、0x46写入成功或配置窗口关闭", 3.5, 16.9, 5.8, 1.55, "state"),
        n("cfg_done", "m_sn_config_complete\n作用：本次上电配置是否已写Flash成功\n置位：0x46写Flash并回读成功\n清除：每次打开新的上电配置窗口", 3.5, 14.55, 5.8, 1.45, "state"),
        n("cfg_time", "m_sn_config_last_activity_ms\n作用：3秒静默超时的计时基准\n更新：收到命令号和payload长度均合法的\n0x40/42/44/46；格式错误不刷新", 3.5, 12.2, 5.8, 1.5, "note"),
        n("capture_due", "g_capture_due（ISR与主循环共享）\n作用：RTC2提出一次图片采集请求\n置位：RTC2周期中断；初始化后首帧也置位\n清除：image_capture_task消费或快速扫描丢弃", 10.0, 19.2, 5.8, 1.5, "event"),
        n("sn_due", "m_sn_broadcast_due（ISR与主循环共享）\n作用：RTC2提出一次普通0x05 SN广播请求\n置位：RTC2周期中断\n清除：广播发送前，或快速扫描开始/结束时丢弃", 10.0, 16.85, 5.8, 1.5, "event"),
        n("prep", "m_capture_prep_state\nIDLE：尚未预热，可接受新的采集请求\nWARMUP：灯、OV7676、ADXL362已启动，等待25ms\n抓图完成后恢复IDLE", 10.0, 14.5, 5.8, 1.5, "state"),
        n("scan", "m_fast_scan_active\n作用：暂停图片/普通SN，执行1800ms密集广播\n置位：等待ACK时收到匹配的0x12\n清除：快速扫描时间到期", 10.0, 12.15, 5.8, 1.45, "state"),
        n("tx_active", "g_image_tx.active\n作用：BEGIN/DATA/END发送状态机是否有帧待处理\n置位：合法JPEG准备完成；ACK超时启动重发\n清除：END发完或CX93510读取失败", 16.5, 19.2, 5.8, 1.5, "state"),
        n("ack", "g_image_tx.awaiting_ack\n作用：首轮END后，Radio是否等待0x10/0x12\n置位：首轮END发送完成并打开RX\n清除：匹配回应、30ms超时或发送失败", 16.5, 16.85, 5.8, 1.5, "state"),
        n("begin", "g_image_tx.legacy_send_begin\n作用：决定第0片前是否连续发送2次BEGIN\n首轮采集完成置true\nACK超时重发DATA+END前置false", 16.5, 14.5, 5.8, 1.45, "state"),
        n("retry", "g_image_tx.retry_count\n作用：区分首轮与ACK超时后的重发轮\n新帧/ACK成功置0；超时准备重发时加1\n非0时END后不再开RX、不等待第二次ACK", 16.5, 12.15, 5.8, 1.55, "note"),
        n("busy", "image_runtime_busy()并不是独立标志位，而是组合判断：\nfast_scan_active || sn_broadcast_due || capture_due ||\nprep_state!=IDLE || image_tx.active || awaiting_ack\n结果为true时主循环保持HFXO/TIMER1运行", 10.0, 8.8, 12.5, 1.75, "note"),
        n("clock", "active_clock_running\napplication_run()里的局部软件记录，不是硬件寄存器状态\ntrue：认为HFXO/TIMER1已启动；false：允许按需重新启动", 10.0, 6.2, 7.8, 1.4, "note"),
    ]
    headers = [
        Header("SN配置窗口标志", 3.5, 21.0, 5.8),
        Header("采集、广播与快速扫描标志", 10.0, 21.0, 6.2),
        Header("图片发送与ACK标志", 16.5, 21.0, 5.8),
    ]
    return PageModel("tx_flags", "TX-08 关键标志位用途与生命周期", 20, 22, nodes, headers, [])


def main():
    models = [
        page_sn_config(),
        page_application_run(),
        page_radio_rx(),
        page_ack_retry(),
        page_capture(),
        page_image_tx(),
        page_scan_sn(),
        page_flag_reference(),
    ]
    # 用户将在Visio中亲自连线：这里只保留排版好的框体和说明文字。
    for model in models:
        model.edges = []
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if not OUTPUT.exists():
        raise FileNotFoundError(OUTPUT)

    # Directly update the user-designated VSDM copy.  Preserve its macro-enabled
    # document content type and every non-page package part; only replace pages,
    # page metadata and the active-window page reference.
    with zipfile.ZipFile(OUTPUT, "r") as source_zip:
        replacements = {
            "visio/pages/pages.xml": vis.pages_index_xml(models),
            "visio/pages/_rels/pages.xml.rels": vis.pages_rels_xml(models),
            "docProps/app.xml": vis.patch_app(source_zip.read("docProps/app.xml"), models),
        }

        content_types = ET.fromstring(source_zip.read("[Content_Types].xml"))
        existing = {
            item.attrib.get("PartName")
            for item in content_types.findall(f"{{{vis.CT}}}Override")
        }
        for index in range(1, len(models) + 1):
            part_name = f"/visio/pages/page{index}.xml"
            if part_name not in existing:
                ET.SubElement(
                    content_types,
                    f"{{{vis.CT}}}Override",
                    {
                        "PartName": part_name,
                        "ContentType": "application/vnd.ms-visio.page+xml",
                    },
                )
        ET.register_namespace("", vis.CT)
        replacements["[Content_Types].xml"] = ET.tostring(
            content_types, encoding="utf-8", xml_declaration=True
        )

        windows = ET.fromstring(source_zip.read("visio/windows.xml"))
        drawing_windows = windows.findall(f"{{{vis.VIS}}}Window")
        if drawing_windows:
            drawing_windows[0].set("Page", "0")
            drawing_windows[0].set("ViewScale", "-1")
            drawing_windows[0].set("ViewCenterX", str(models[0].width / 2))
            drawing_windows[0].set("ViewCenterY", str(models[0].height / 2))
        ET.register_namespace("", vis.VIS)
        ET.register_namespace("r", vis.REL)
        replacements["visio/windows.xml"] = ET.tostring(
            windows, encoding="utf-8", xml_declaration=True
        )

        for index, model in enumerate(models, 1):
            replacements[f"visio/pages/page{index}.xml"] = vis.page_xml(model)

        with tempfile.NamedTemporaryFile(
            delete=False, suffix=".vsdm", dir=OUTPUT.parent
        ) as temp_file:
            temp_path = Path(temp_file.name)

        try:
            with zipfile.ZipFile(
                temp_path, "w", compression=zipfile.ZIP_DEFLATED
            ) as output_zip:
                written = set()
                for info in source_zip.infolist():
                    name = info.filename
                    if re.fullmatch(r"visio/pages/page\d+\.xml", name):
                        continue
                    if re.fullmatch(r"visio/pages/_rels/page\d+\.xml\.rels", name):
                        continue
                    data = replacements.get(name, source_zip.read(name))
                    output_zip.writestr(name, data)
                    written.add(name)
                for name, data in replacements.items():
                    if name not in written:
                        output_zip.writestr(name, data)

            with zipfile.ZipFile(temp_path, "r") as check_zip:
                bad_member = check_zip.testzip()
                if bad_member:
                    raise RuntimeError(f"Corrupt VSDM package member: {bad_member}")
                for index in range(1, len(models) + 1):
                    ET.fromstring(check_zip.read(f"visio/pages/page{index}.xml"))
                document_type = ET.fromstring(
                    check_zip.read("[Content_Types].xml")
                ).find(f"{{{vis.CT}}}Override[@PartName='/visio/document.xml']")
                if document_type is None or "macroEnabled" not in document_type.attrib.get(
                    "ContentType", ""
                ):
                    raise RuntimeError("VSDM macro-enabled content type was not preserved")

            # Close the source package before replacing it on Windows; an open
            # ZipFile handle prevents os.replace() from updating the VSDM copy.
            source_zip.close()
            temp_path.replace(OUTPUT)
        finally:
            if temp_path.exists():
                temp_path.unlink()

    vis.OUTPUT = OUTPUT
    vis.verify(models)


if __name__ == "__main__":
    main()
