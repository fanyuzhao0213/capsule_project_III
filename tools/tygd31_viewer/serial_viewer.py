"""
serial_viewer.py
串口实时显示：连接 STM32 UART3，解析 0xFF 0x55 0x12 0x34 帧，
提取 JPEG 后显示。
"""

from __future__ import annotations
import io
import time
from typing import List
from PyQt5 import QtCore, QtGui, QtWidgets

from frame_parser import (
    ParseState,
    ImageFrame,
    CMD_IMG_FORWARD,
    CMD_TEXT_INFO,
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
        self.cmb_baud.setCurrentText("1000000")

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
            self._serial = serial.Serial(port, baud, timeout=0.1)
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

    def _close(self):
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
            self.txt_info.setPlainText(device_info_to_text(frame.device_info))
            self._log(f"[图像] cmd=0x{frame.cmd:02X} "
                      f"大小={frame.jpeg_size}B")
        elif frame.cmd == CMD_TEXT_INFO:
            try:
                txt = frame.jpeg.decode("ascii", errors="replace")
            except Exception:
                txt = repr(frame.jpeg)
            self._log(f"[信息] {txt.strip()}")
        else:
            self._log(f"[未知] cmd=0x{frame.cmd:02X} 大小={frame.jpeg_size}B")

        # 更新统计
        elapsed = time.time() - self._last_rx_time if self._last_rx_time else 0
        self.lbl_stats.setText(
            f"RX: {self._rx_count} B | 帧: {self._frame_count} | "
            f"最近活动: {time.strftime('%H:%M:%S')}"
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


