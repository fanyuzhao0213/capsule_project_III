"""
serial_viewer.py
串口实时显示：连接 STM32 UART3，解析 0x5A 0x41 0x59 0x53（ZAYS）帧，
提取 JPEG 后显示。
"""

from __future__ import annotations
import time
import re
from collections import deque
from datetime import datetime
from typing import List
from PyQt5 import QtCore, QtGui, QtWidgets

from frame_parser import (
    ParseState,
    ImageFrame,
    CMD_IMG_FORWARD,
    CMD_SN_BROADCAST,
    CMD_TEXT_INFO,
    CMD_RTC_SET,
    CMD_SN_QUERY_REQUEST,
    CMD_SN_QUERY_RESPONSE,
    CMD_SN_UNBIND_REQUEST,
    CMD_SN_UNBIND_RESPONSE,
    CMD_SN_BIND_REQUEST,
    CMD_SN_BIND_RESPONSE,
    CMD_SN_PREPARE_REQUEST,
    CMD_SN_PREPARE_RESPONSE,
    CMD_DEVICE_ID_QUERY_REQUEST,
    CMD_DEVICE_ID_QUERY_RESPONSE,
    CMD_SN_SET_REQUEST,
    CMD_SN_SET_RESPONSE,
    CMD_SN_CONFIRM_REQUEST,
    CMD_SN_CONFIRM_RESPONSE,
    CMD_CONTROL_ERROR_RESPONSE,
    CMD_LED_MODE_REQUEST,
    CMD_LED_MODE_RESPONSE,
    CMD_REBOOT_REQUEST,
    encode_capsule_sn,
    make_control_frame,
    rtc_payload,
    device_info_to_text,
)


class SerialViewer(QtWidgets.QWidget):
    """串口实时显示页签。"""

    def __init__(self, parent=None):
        super().__init__(parent)

        # --- 串口配置区 ---
        gb = QtWidgets.QGroupBox("串口设置")
        form = QtWidgets.QFormLayout(gb)

        self.cmb_port = QtWidgets.QComboBox()
        self.cmb_port.setEditable(True)
        self.cmb_port.addItems(self._scan_ports())
        self.cmb_port.setMinimumWidth(140)

        self.cmb_baud = QtWidgets.QComboBox()
        self.cmb_baud.addItems(["9600", "115200", "460800", "921600", "1000000"])
        self.cmb_baud.setCurrentText("921600")

        self.btn_refresh = QtWidgets.QPushButton("刷新端口")
        self.btn_refresh.clicked.connect(self._refresh_ports)

        self.btn_connect = QtWidgets.QPushButton("打开串口")
        self.btn_connect.setCheckable(True)
        self.btn_connect.toggled.connect(self._on_toggle_connect)

        row1 = QtWidgets.QHBoxLayout()
        row1.addWidget(QtWidgets.QLabel("端口:"))
        row1.addWidget(self.cmb_port)
        row1.addWidget(QtWidgets.QLabel("波特率:"))
        row1.addWidget(self.cmb_baud)
        row1.addWidget(self.btn_refresh)
        row1.addWidget(self.btn_connect)
        row1.addStretch()
        form.addRow(row1)

        self.cmb_sn = QtWidgets.QComboBox()
        self.cmb_sn.setEditable(True)
        self.cmb_sn.setMinimumWidth(240)
        self.cmb_sn.setPlaceholderText("输入16位十六进制SN，或选择发现的胶囊")
        self.btn_bind = QtWidgets.QPushButton("绑定并同步时间")
        self.btn_bind.clicked.connect(self._bind)
        self.btn_unbind = QtWidgets.QPushButton("解绑")
        self.btn_unbind.clicked.connect(self._unbind)
        self.btn_query = QtWidgets.QPushButton("查询绑定")
        self.btn_query.clicked.connect(self._query_binding)
        binding_row = QtWidgets.QHBoxLayout()
        binding_row.addWidget(QtWidgets.QLabel("胶囊SN:"))
        binding_row.addWidget(self.cmb_sn, 1)
        binding_row.addWidget(self.btn_bind)
        binding_row.addWidget(self.btn_unbind)
        binding_row.addWidget(self.btn_query)
        form.addRow(binding_row)
        self.lbl_binding = QtWidgets.QLabel("绑定状态：未查询")
        form.addRow(self.lbl_binding)

        self.cmb_factory_prefix = QtWidgets.QComboBox()
        self.cmb_factory_prefix.addItems(["1", "6"])
        self.edit_factory_digits = QtWidgets.QLineEdit("0" * 13)
        self.edit_factory_digits.setMaxLength(13)
        self.edit_factory_digits.setValidator(
            QtGui.QRegularExpressionValidator(
                QtCore.QRegularExpression("[0-9]{0,13}"), self))
        self.edit_factory_digits.setFixedWidth(180)
        self.btn_factory_sn = QtWidgets.QPushButton("配置胶囊SN")
        self.btn_factory_sn.clicked.connect(self._configure_capsule_sn)
        factory_row = QtWidgets.QHBoxLayout()
        factory_row.addWidget(QtWidgets.QLabel("新胶囊SN: Y"))
        factory_row.addWidget(self.cmb_factory_prefix)
        factory_row.addWidget(self.edit_factory_digits)
        factory_row.addWidget(self.btn_factory_sn)
        factory_row.addStretch()
        form.addRow(factory_row)
        self.lbl_factory_sn = QtWidgets.QLabel(
            "默认 Y1" + "0" * 13 + "；Y占1字节，后14位数字按8421码编码")
        form.addRow(self.lbl_factory_sn)

        self.cmb_led_mode = QtWidgets.QComboBox()
        self.cmb_led_mode.addItem("打开", 1)
        self.cmb_led_mode.addItem("关闭", 0)
        self.btn_led = QtWidgets.QPushButton("LED控制")
        self.btn_led.clicked.connect(self._set_led_mode)
        self.btn_reboot = QtWidgets.QPushButton("重启胶囊")
        self.btn_reboot.clicked.connect(self._reboot_capsule)
        runtime_row = QtWidgets.QHBoxLayout()
        runtime_row.addWidget(QtWidgets.QLabel("LED模式:"))
        runtime_row.addWidget(self.cmb_led_mode)
        runtime_row.addWidget(self.btn_led)
        runtime_row.addSpacing(18)
        runtime_row.addWidget(self.btn_reboot)
        runtime_row.addStretch()
        form.addRow(runtime_row)
        self.lbl_runtime = QtWidgets.QLabel("运行期控制：请先查询或绑定胶囊")
        form.addRow(self.lbl_runtime)

        # --- 统计区 ---
        self.lbl_stats = QtWidgets.QLabel("未连接")
        self.lbl_stats.setStyleSheet("color: #666;")
        form.addRow(self.lbl_stats)

        # --- 主区域：左图右日志 ---
        splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal)

        # 左：图像显示
        left = QtWidgets.QWidget()
        lv = QtWidgets.QVBoxLayout(left)
        lv.setContentsMargins(0, 0, 0, 0)
        self.lbl_image = QtWidgets.QLabel("等待图像…")
        self.lbl_image.setAlignment(QtCore.Qt.AlignCenter)
        self.lbl_image.setMinimumSize(480, 360)
        self.lbl_image.setStyleSheet("background:#222;color:#888;border:1px solid #444;")
        lv.addWidget(self.lbl_image, 1)

        self.btn_save = QtWidgets.QPushButton("保存当前图像")
        self.btn_save.clicked.connect(self._save_current)
        self.btn_save.setEnabled(False)
        lv.addWidget(self.btn_save)

        # 右：日志 / 设备信息
        right = QtWidgets.QWidget()
        rv = QtWidgets.QVBoxLayout(right)
        rv.setContentsMargins(0, 0, 0, 0)
        rv.addWidget(QtWidgets.QLabel("设备信息:"))
        self.txt_info = QtWidgets.QPlainTextEdit()
        self.txt_info.setReadOnly(True)
        self.txt_info.setMaximumBlockCount(100)
        rv.addWidget(self.txt_info, 1)

        rv.addWidget(QtWidgets.QLabel("事件日志:"))
        self.txt_log = QtWidgets.QPlainTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMaximumBlockCount(200)
        rv.addWidget(self.txt_log, 1)

        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 2)

        root = QtWidgets.QVBoxLayout(self)
        root.addWidget(gb)
        root.addWidget(splitter, 1)

        # --- 后台状态 ---
        self._serial = None
        self._thread = None
        self._parser = ParseState()
        self._current_frame: ImageFrame | None = None
        self._rx_count = 0
        self._frame_count = 0
        self._last_rx_time = 0.0
        self._device_info_history = deque(maxlen=10)
        self._image_times = deque(maxlen=30)
        self._pending_command = None
        self._pending_sn = None
        self._bound_sn = None
        self._factory_sn_target = None
        self._control_timer = QtCore.QTimer(self)
        self._control_timer.setSingleShot(True)
        self._control_timer.timeout.connect(self._control_timeout)
        self._update_control_buttons()

    def _update_control_buttons(self):
        ready = self._serial is not None and self._pending_command is None
        for button in (self.btn_bind, self.btn_unbind, self.btn_query):
            button.setEnabled(ready)
        self.btn_factory_sn.setEnabled(ready)
        runtime_ready = ready and self._bound_sn is not None
        self.btn_led.setEnabled(runtime_ready)
        self.btn_reboot.setEnabled(runtime_ready)

    def _send_control(self, cmd: int, payload: bytes = b""):
        if self._serial is None:
            return False
        try:
            data = make_control_frame(cmd, payload)
            if self._serial.write(data) != len(data):
                raise IOError("串口写入不完整")
            self._log(f"[控制发送] {data.hex(' ').upper()}")
            return True
        except Exception as exc:
            self._log(f"[控制] 发送失败: {exc}")
            QtWidgets.QMessageBox.warning(self, "发送失败", str(exc))
            return False

    def _start_request(self, cmd: int, payload: bytes = b"", sn: bytes = None,
                       timeout_ms: int = 3000):
        if self._pending_command is not None or self._serial is None:
            return False
        if not self._send_control(cmd, payload):
            return False
        self._pending_command = cmd
        self._pending_sn = sn
        self._update_control_buttons()
        self._control_timer.start(timeout_ms)
        return True

    def _finish_request(self):
        self._control_timer.stop()
        self._pending_command = None
        self._pending_sn = None
        self._update_control_buttons()

    def _control_timeout(self):
        timed_out_command = self._pending_command
        self._log(f"[控制] 0x{timed_out_command:02X} 应答超时")
        self._finish_request()
        if self._factory_sn_target is not None:
            self._factory_sn_target = None
            self.lbl_factory_sn.setText("胶囊SN配置超时；请重新给胶囊上电后重试")
            return
        if timed_out_command == CMD_LED_MODE_REQUEST:
            self.lbl_runtime.setText("LED模式：未收到4B回包，执行状态未知")
            return
        if timed_out_command in (CMD_SN_BIND_REQUEST, CMD_SN_UNBIND_REQUEST):
            if self._start_request(CMD_SN_QUERY_REQUEST):
                self.lbl_binding.setText("绑定状态：正在查询RX实际状态")
                self._log("[控制] 已自动查询RX实际绑定状态")
                return
        self.lbl_binding.setText("绑定状态：未知（应答超时）")

    def _bind(self):
        raw = self.cmb_sn.currentText().strip()
        compact = re.sub(r"[\s:-]", "", raw)
        if not re.fullmatch(r"[0-9a-fA-F]{16}", compact):
            QtWidgets.QMessageBox.warning(self, "SN格式错误", "胶囊SN必须是8字节（16位十六进制）。")
            return
        sn = bytes.fromhex(compact)
        now = datetime.now()
        if not self._send_control(CMD_RTC_SET, rtc_payload(now)):
            return
        self._log(f"[RTC] 已发送电脑本地时间 {now:%Y-%m-%d %H:%M:%S}；STM对此命令无独立应答")
        if self._start_request(CMD_SN_BIND_REQUEST, sn, sn):
            self.lbl_binding.setText("绑定状态：等待RX应答")
        else:
            self.lbl_binding.setText("绑定状态：未知（绑定命令未发送）")

    def _unbind(self):
        if self._start_request(CMD_SN_UNBIND_REQUEST):
            self.lbl_binding.setText("绑定状态：等待RX应答")

    def _query_binding(self):
        self._start_request(CMD_SN_QUERY_REQUEST)

    def _set_led_mode(self):
        if self._bound_sn is None:
            return
        mode = self.cmb_led_mode.currentData()
        if self._start_request(CMD_LED_MODE_REQUEST,
                               self._bound_sn + bytes((mode,)),
                               self._bound_sn, timeout_ms=8000):
            self.lbl_runtime.setText("LED模式：等待4B回包")

    def _reboot_capsule(self):
        if self._bound_sn is None:
            return
        if self._send_control(CMD_REBOOT_REQUEST, self._bound_sn):
            self.lbl_runtime.setText("重启指令已发送；协议无回包")
            self._log("[重启] RX将在下一个匹配SN的图片END窗口下发；协议无回包")

    def _configure_capsule_sn(self):
        digits = self.edit_factory_digits.text()
        if not re.fullmatch(r"[0-9]{13}", digits):
            QtWidgets.QMessageBox.warning(self, "SN格式错误", "请输入13位数字。")
            return
        sn_text = "Y" + self.cmb_factory_prefix.currentText() + digits
        self._factory_sn_target = encode_capsule_sn(sn_text)
        if self._start_request(CMD_SN_PREPARE_REQUEST, timeout_ms=1200):
            self.lbl_factory_sn.setText(f"正在配置 {sn_text}：等待胶囊准备回包")
        else:
            self._factory_sn_target = None

    def _factory_sn_next(self, command: int, payload: bytes = b""):
        if not self._start_request(command, payload, timeout_ms=1200):
            self._factory_sn_target = None
            self.lbl_factory_sn.setText("胶囊SN配置中断：指令发送失败")

    def _factory_sn_fail(self, message: str):
        self._log(f"[胶囊SN] {message}")
        self.lbl_factory_sn.setText(f"胶囊SN配置失败：{message}")
        self._factory_sn_target = None
        self._finish_request()

    # ------------------------------------------------------------------
    # 端口扫描
    # ------------------------------------------------------------------
    def _scan_ports(self) -> List[str]:
        try:
            import serial.tools.list_ports as lp
            return [p.device for p in lp.comports()]
        except Exception:
            return ["COM1", "COM3", "/dev/ttyUSB0"]

    def _refresh_ports(self):
        self.cmb_port.clear()
        self.cmb_port.addItems(self._scan_ports())

    # ------------------------------------------------------------------
    # 连接管理
    # ------------------------------------------------------------------
    def _on_toggle_connect(self, on: bool):
        if on:
            self._open()
        else:
            self._close()

    def _open(self):
        import serial  # noqa
        try:
            port = self.cmb_port.currentText().strip()
            baud = int(self.cmb_baud.currentText())
            self._serial = serial.Serial(port, baud, timeout=0.1, write_timeout=1)
        except Exception as e:
            QtWidgets.QMessageBox.critical(self, "串口错误", f"无法打开: {e}")
            self.btn_connect.setChecked(False)
            return

        self._thread = _SerialReader(self._serial)
        self._thread.frame_received.connect(self._on_frame)
        self._thread.bytes_received.connect(self._on_bytes)
        self._thread.start()
        self.btn_connect.setText("关闭串口")
        self.lbl_stats.setText(f"已连接: {port} @ {baud}")
        self.lbl_stats.setStyleSheet("color:#0a0;")
        self.lbl_binding.setText("绑定状态：未查询")
        self._bound_sn = None
        self._factory_sn_target = None
        self.lbl_factory_sn.setText(
            "默认 Y1" + "0" * 13 + "；胶囊上电后3秒内开始配置")
        self.lbl_runtime.setText("运行期控制：请先查询或绑定胶囊")
        self._update_control_buttons()

    def _close(self):
        self._finish_request()
        self._factory_sn_target = None
        if self._thread is not None:
            self._thread.stop()
            self._thread.wait(500)
            self._thread = None
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:
                pass
            self._serial = None
        self.btn_connect.setText("打开串口")
        self.lbl_stats.setText("未连接")
        self.lbl_stats.setStyleSheet("color:#666;")
        self.lbl_binding.setText("绑定状态：未连接")
        self.lbl_factory_sn.setText("胶囊SN配置：未连接")
        self._bound_sn = None
        self.lbl_runtime.setText("运行期控制：未连接")
        self._update_control_buttons()

    # ------------------------------------------------------------------
    # 数据回调
    # ------------------------------------------------------------------
    def _on_bytes(self, n: int):
        self._rx_count += n
        self._last_rx_time = time.time()

    def _on_frame(self, frame: ImageFrame):
        self._frame_count += 1
        if frame.cmd == CMD_IMG_FORWARD and frame.jpeg_size > 0:
            self._current_frame = frame
            self._show_image(frame.jpeg)
            self.btn_save.setEnabled(True)
            previous = (self._device_info_history[-1][1]
                        if self._device_info_history else None)
            self._device_info_history.append(
                (self._frame_count, bytes(frame.device_info), frame.timestamp or time.time())
            )
            blocks = []
            history = list(self._device_info_history)
            for index, (number, info, received_at) in enumerate(history):
                prior = history[index - 1][1] if index > 0 else None
                blocks.append(device_info_to_text(
                    info, prior, number, received_at
                ))
            separator = "\n\n" + ("=" * 72) + "\n\n"
            self.txt_info.setPlainText(separator.join(blocks))
            self.txt_info.verticalScrollBar().setValue(
                self.txt_info.verticalScrollBar().maximum()
            )
            self._image_times.append(frame.timestamp or time.time())
            self._log(f"[图像] cmd=0x{frame.cmd:02X} "
                      f"大小={frame.jpeg_size}B")
        elif frame.cmd == CMD_TEXT_INFO:
            try:
                txt = frame.jpeg.decode("ascii", errors="replace")
            except Exception:
                txt = repr(frame.jpeg)
            self._log(f"[信息] {txt.strip()}")
        elif frame.cmd == CMD_SN_BROADCAST and len(frame.jpeg) == 8:
            sn = frame.jpeg.hex(" ").upper()
            self._log(f"[发现设备] 胶囊SN: {sn}")
            if self.cmb_sn.findText(sn) < 0:
                old_text = self.cmb_sn.currentText()
                self.cmb_sn.addItem(sn)
                if old_text:
                    self.cmb_sn.setCurrentText(old_text)
        elif (self._pending_command == CMD_SN_QUERY_REQUEST and
              frame.cmd == CMD_SN_QUERY_RESPONSE and len(frame.jpeg) == 8):
            sn = frame.jpeg
            if sn == bytes(8):
                self._bound_sn = None
                self.lbl_binding.setText("绑定状态：未绑定")
                self._log("[绑定] RX当前未绑定")
            else:
                self._bound_sn = sn
                sn_text = sn.hex(" ").upper()
                self.lbl_binding.setText(f"绑定状态：已绑定 {sn_text}")
                self.cmb_sn.setCurrentText(sn_text)
                self._log(f"[绑定] RX当前绑定 {sn_text}")
            self._finish_request()
        elif (self._pending_command == CMD_SN_BIND_REQUEST and
              frame.cmd == CMD_SN_BIND_RESPONSE and len(frame.jpeg) == 1):
            if frame.jpeg[0] == 1:
                self._bound_sn = self._pending_sn
                sn_text = self._pending_sn.hex(" ").upper()
                self.lbl_binding.setText(f"绑定状态：已绑定 {sn_text}")
                self._log(f"[绑定] 成功：{sn_text}")
            else:
                self._bound_sn = None
                self.lbl_binding.setText("绑定状态：绑定失败")
                self._log("[绑定] RX返回失败")
            self._finish_request()
        elif (self._pending_command == CMD_SN_UNBIND_REQUEST and
              frame.cmd == CMD_SN_UNBIND_RESPONSE and len(frame.jpeg) == 1):
            if frame.jpeg[0] == 1:
                self._bound_sn = None
                self.lbl_binding.setText("绑定状态：未绑定")
                self._log("[解绑] 成功")
            else:
                self.lbl_binding.setText("绑定状态：解绑失败")
                self._log("[解绑] RX返回失败")
            self._finish_request()
        elif (self._factory_sn_target is not None and
              frame.cmd == CMD_CONTROL_ERROR_RESPONSE):
            self._factory_sn_fail("胶囊返回控制错误；请确认处于上电配置窗口")
        elif (self._factory_sn_target is not None and
              self._pending_command == CMD_SN_PREPARE_REQUEST and
              frame.cmd == CMD_SN_PREPARE_RESPONSE and len(frame.jpeg) == 0):
            self._finish_request()
            self.lbl_factory_sn.setText("胶囊SN配置：读取胶囊芯片ID")
            self._factory_sn_next(CMD_DEVICE_ID_QUERY_REQUEST)
        elif (self._factory_sn_target is not None and
              self._pending_command == CMD_DEVICE_ID_QUERY_REQUEST and
              frame.cmd == CMD_DEVICE_ID_QUERY_RESPONSE and len(frame.jpeg) == 8):
            device_id = frame.jpeg
            self._finish_request()
            self.lbl_factory_sn.setText("胶囊SN配置：暂存新SN")
            self._factory_sn_next(CMD_SN_SET_REQUEST,
                                  device_id + self._factory_sn_target)
        elif (self._factory_sn_target is not None and
              self._pending_command == CMD_SN_SET_REQUEST and
              frame.cmd == CMD_SN_SET_RESPONSE and
              frame.jpeg == self._factory_sn_target):
            self._finish_request()
            self.lbl_factory_sn.setText("胶囊SN配置：确认写入")
            self._factory_sn_next(CMD_SN_CONFIRM_REQUEST)
        elif (self._factory_sn_target is not None and
              self._pending_command == CMD_SN_CONFIRM_REQUEST and
              frame.cmd == CMD_SN_CONFIRM_RESPONSE and len(frame.jpeg) == 0):
            sn_text = "Y" + self._factory_sn_target[1:].hex().upper()
            sn_hex = self._factory_sn_target.hex(" ").upper()
            self._factory_sn_target = None
            self._finish_request()
            self.cmb_sn.setCurrentText(sn_hex)
            self._bound_sn = None
            self._update_control_buttons()
            self.lbl_binding.setText("绑定状态：请将RX重新绑定到新SN")
            self.lbl_factory_sn.setText(f"胶囊SN配置成功：{sn_text}")
            self._log(f"[胶囊SN] 已确认写入 {sn_text}（{sn_hex}）")
        elif (self._pending_command == CMD_LED_MODE_REQUEST and
              frame.cmd == CMD_LED_MODE_RESPONSE and len(frame.jpeg) == 10 and
              frame.jpeg[:8] == self._pending_sn):
            result, mode = frame.jpeg[8], frame.jpeg[9]
            if result == 1 and mode in (0, 1):
                label = "打开" if mode == 1 else "关闭"
                self.cmb_led_mode.setCurrentIndex(self.cmb_led_mode.findData(mode))
                self.lbl_runtime.setText(f"LED模式：{label}（胶囊已确认）")
                self._log(f"[LED] 设置成功，当前模式：{label}")
            else:
                self.lbl_runtime.setText("LED模式：设置失败")
                self._log(f"[LED] 设置失败，返回模式=0x{mode:02X}")
            self._finish_request()
        elif frame.cmd in (CMD_SN_QUERY_RESPONSE, CMD_SN_BIND_RESPONSE,
                           CMD_SN_UNBIND_RESPONSE, CMD_LED_MODE_RESPONSE,
                           CMD_SN_PREPARE_RESPONSE, CMD_DEVICE_ID_QUERY_RESPONSE,
                           CMD_SN_SET_RESPONSE, CMD_SN_CONFIRM_RESPONSE,
                           CMD_CONTROL_ERROR_RESPONSE):
            self._log(f"[控制] 未匹配应答 cmd=0x{frame.cmd:02X} 数据={frame.jpeg.hex(' ').upper()}")
        else:
            self._log(f"[未知] cmd=0x{frame.cmd:02X} 大小={frame.jpeg_size}B")

        # 更新统计
        elapsed = time.time() - self._last_rx_time if self._last_rx_time else 0
        fps = 0.0
        if len(self._image_times) >= 2:
            duration = self._image_times[-1] - self._image_times[0]
            if duration > 0:
                fps = (len(self._image_times) - 1) / duration
        self.lbl_stats.setText(
            f"RX: {self._rx_count} B | 帧: {self._frame_count} | "
            f"图像速率: {fps:.2f} fps | 最近活动: {time.strftime('%H:%M:%S')}"
        )

    # ------------------------------------------------------------------
    # 显示 JPEG
    # ------------------------------------------------------------------
    def _show_image(self, jpeg_data: bytes):
        try:
            pix = QtGui.QPixmap()
            if not pix.loadFromData(jpeg_data, "JPEG"):
                raise ValueError("JPEG 解码失败")
        except Exception as e:
            self.lbl_image.setText(f"图像解码失败:\n{e}")
            self.lbl_image.setPixmap(QtGui.QPixmap())
            return

        # 等比缩放到 label 大小
        scaled = pix.scaled(
            self.lbl_image.size(),
            QtCore.Qt.KeepAspectRatio,
            QtCore.Qt.SmoothTransformation,
        )
        self.lbl_image.setPixmap(scaled)

    def _save_current(self):
        if self._current_frame is None or self._current_frame.jpeg_size == 0:
            return
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self,
            "保存图像",
            f"image_{time.strftime('%Y%m%d_%H%M%S')}.jpg",
            "JPEG (*.jpg);;All files (*)",
        )
        if not path:
            return
        try:
            with open(path, "wb") as f:
                f.write(self._current_frame.jpeg)
            self._log(f"[保存] {path} ({self._current_frame.jpeg_size} B)")
        except OSError as e:
            QtWidgets.QMessageBox.critical(self, "保存失败", str(e))

    def _log(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        self.txt_log.appendPlainText(f"[{ts}] {msg}")


# ============================================================
# 后台读取线程
# ============================================================
class _SerialReader(QtCore.QThread):
    frame_received = QtCore.pyqtSignal(object)  # ImageFrame
    bytes_received = QtCore.pyqtSignal(int)

    def __init__(self, serial_obj):
        super().__init__()
        self._serial = serial_obj
        self._stop = False
        self._parser = ParseState()

    def stop(self):
        self._stop = True

    def run(self):
        while not self._stop:
            try:
                data = self._serial.read(4096)
            except Exception:
                break
            if not data:
                continue
            self.bytes_received.emit(len(data))
            frames = self._parser.feed(data)
            for f in frames:
                f.timestamp = time.time()
                self.frame_received.emit(f)

