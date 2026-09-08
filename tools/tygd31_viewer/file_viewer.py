"""
file_viewer.py
U 盘文件查看页签：浏览 SD 卡 .YS 文件，按帧解码并显示 JPEG。

使用方法：
1. 把 STM32 SD 卡挂到电脑（读卡器/USB），或拷贝 .YS 文件到本地目录
2. 点击 "选择 U 盘/目录" 选定根目录
3. 左侧列表自动列出所有 Y<SN>.YS 文件
4. 双击或点击 "解析" 在右侧浏览图像
"""

from __future__ import annotations
import os
import time
from typing import List, Optional
from PyQt5 import QtCore, QtGui, QtWidgets

from frame_parser import (
    ImageFrame,
    parse_ys_file,
    list_ys_files,
    device_info_to_text,
    YS_FRAME_SIZE,
    JPEG_HEADER_LEN,
)


class FileViewer(QtWidgets.QWidget):
    """U 盘 .YS 文件查看页签。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._root: Optional[str] = None
        self._current_file: Optional[str] = None
        self._frames: List[ImageFrame] = []
        self._index = 0

        # --- 顶部：路径选择 ---
        gb = QtWidgets.QGroupBox("U 盘/目录")
        h = QtWidgets.QHBoxLayout(gb)
        self.btn_choose = QtWidgets.QPushButton("选择目录…")
        self.btn_choose.clicked.connect(self._choose_dir)
        self.btn_refresh = QtWidgets.QPushButton("刷新")
        self.btn_refresh.clicked.connect(self._refresh)
        self.lbl_path = QtWidgets.QLabel("(未选择)")
        self.lbl_path.setStyleSheet("color:#666;")
        h.addWidget(self.btn_choose)
        h.addWidget(self.btn_refresh)
        h.addWidget(self.lbl_path, 1)

        # --- 中间：左文件列表，右浏览区 ---
        splitter = QtWidgets.QSplitter(QtCore.Qt.Horizontal)

        # 左：文件列表
        left = QtWidgets.QWidget()
        lv = QtWidgets.QVBoxLayout(left)
        lv.setContentsMargins(0, 0, 0, 0)
        lv.addWidget(QtWidgets.QLabel(".YS 文件列表:"))
        self.list_files = QtWidgets.QListWidget()
        self.list_files.itemDoubleClicked.connect(self._on_select_file)
        lv.addWidget(self.list_files, 1)
        splitter.addWidget(left)

        # 右：帧索引 + 图像 + 信息
        right = QtWidgets.QWidget()
        rv = QtWidgets.QVBoxLayout(right)
        rv.setContentsMargins(0, 0, 0, 0)

        # 帧导航
        nav = QtWidgets.QHBoxLayout()
        self.btn_prev = QtWidgets.QPushButton("上一帧")
        self.btn_prev.clicked.connect(self._prev_frame)
        self.lbl_index = QtWidgets.QLabel("0 / 0")
        self.btn_next = QtWidgets.QPushButton("下一帧")
        self.btn_next.clicked.connect(self._next_frame)
        nav.addWidget(self.btn_prev)
        nav.addWidget(self.lbl_index, 1)
        nav.addWidget(self.btn_next)
        rv.addLayout(nav)

        # 图像
        self.lbl_image = QtWidgets.QLabel("请先选择 .YS 文件…")
        self.lbl_image.setAlignment(QtCore.Qt.AlignCenter)
        self.lbl_image.setMinimumSize(480, 360)
        self.lbl_image.setStyleSheet("background:#222;color:#888;border:1px solid #444;")
        rv.addWidget(self.lbl_image, 1)

        # 操作按钮
        btn_row = QtWidgets.QHBoxLayout()
        self.btn_export = QtWidgets.QPushButton("导出当前帧为 JPG")
        self.btn_export.clicked.connect(self._export_current)
        self.btn_export.setEnabled(False)
        self.btn_export_all = QtWidgets.QPushButton("导出全部帧为 JPG…")
        self.btn_export_all.clicked.connect(self._export_all)
        self.btn_export_all.setEnabled(False)
        btn_row.addWidget(self.btn_export)
        btn_row.addWidget(self.btn_export_all)
        btn_row.addStretch()
        rv.addLayout(btn_row)

        # 设备信息
        rv.addWidget(QtWidgets.QLabel("设备信息:"))
        self.txt_info = QtWidgets.QPlainTextEdit()
        self.txt_info.setReadOnly(True)
        self.txt_info.setMaximumBlockCount(50)
        rv.addWidget(self.txt_info, 1)

        splitter.addWidget(right)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 3)

        root = QtWidgets.QVBoxLayout(self)
        root.addWidget(gb)
        root.addWidget(splitter, 1)

        self._set_nav_enabled(False)

    # ------------------------------------------------------------------
    # 目录选择 & 文件列表
    # ------------------------------------------------------------------
    def _choose_dir(self):
        path = QtWidgets.QFileDialog.getExistingDirectory(
            self,
            "选择 U 盘/目录（含 .YS 文件）",
            self._root or "",
        )
        if not path:
            return
        self._set_root(path)

    def _refresh(self):
        if self._root:
            self._set_root(self._root)

    def _set_root(self, root: str):
        self._root = root
        self.lbl_path.setText(root)
        self.list_files.clear()
        for p in list_ys_files(root):
            self.list_files.addItem(os.path.basename(p))
        self._log(f"[目录] 列出 {self.list_files.count()} 个 .YS 文件")

    def _on_select_file(self, item):
        if self._root is None:
            return
        name = item.text()
        path = os.path.join(self._root, name)
        self._load_file(path)

    # ------------------------------------------------------------------
    # 帧加载 & 显示
    # ------------------------------------------------------------------
    def _load_file(self, path: str):
        self._log(f"[解析] {path} …")
        t0 = time.time()
        frames = parse_ys_file(path)
        dt = time.time() - t0
        if not frames:
            self._log(f"[错误] 解析失败或文件为空: {path}")
            return
        self._current_file = path
        self._frames = frames
        self._index = 0
        self._set_nav_enabled(True)
        self.btn_export_all.setEnabled(True)
        self._log(f"[完成] 解析 {len(frames)} 帧，耗时 {dt:.2f} s")
        self._show_frame()

    def _prev_frame(self):
        if not self._frames:
            return
        self._index = (self._index - 1) % len(self._frames)
        self._show_frame()

    def _next_frame(self):
        if not self._frames:
            return
        self._index = (self._index + 1) % len(self._frames)
        self._show_frame()

    def _show_frame(self):
        if not self._frames:
            return
        f = self._frames[self._index]
        self.lbl_index.setText(f"{self._index + 1} / {len(self._frames)}")
        self.txt_info.setPlainText(device_info_to_text(f.device_info))
        self.btn_export.setEnabled(True)
        # 显示 JPEG（解析后的 jpeg 已是 header + data 拼接的完整文件）
        try:
            pix = QtGui.QPixmap()
            if not pix.loadFromData(f.jpeg, "JPEG"):
                raise ValueError("JPEG 解码失败")
            scaled = pix.scaled(
                self.lbl_image.size(),
                QtCore.Qt.KeepAspectRatio,
                QtCore.Qt.SmoothTransformation,
            )
            self.lbl_image.setPixmap(scaled)
        except Exception as e:
            self.lbl_image.setText(f"图像解码失败:\n{e}\n帧大小: {len(f.jpeg)} B")
            self.lbl_image.setPixmap(QtGui.QPixmap())

    def _set_nav_enabled(self, on: bool):
        self.btn_prev.setEnabled(on)
        self.btn_next.setEnabled(on)
        self.btn_export.setEnabled(False)

    # ------------------------------------------------------------------
    # 导出
    # ------------------------------------------------------------------
    def _export_current(self):
        if not self._frames:
            return
        default_name = f"{os.path.splitext(os.path.basename(self._current_file))[0]}_frame{self._index + 1:04d}.jpg"
        path, _ = QtWidgets.QFileDialog.getSaveFileName(
            self, "导出当前帧", default_name, "JPEG (*.jpg)"
        )
        if not path:
            return
        try:
            with open(path, "wb") as fp:
                fp.write(self._frames[self._index].jpeg)
            self._log(f"[导出] {path}")
        except OSError as e:
            QtWidgets.QMessageBox.critical(self, "导出失败", str(e))

    def _export_all(self):
        if not self._frames:
            return
        out_dir = QtWidgets.QFileDialog.getExistingDirectory(
            self, "选择导出目录", ""
        )
        if not out_dir:
            return
        base = os.path.splitext(os.path.basename(self._current_file))[0]
        try:
            for i, f in enumerate(self._frames):
                p = os.path.join(out_dir, f"{base}_frame{i + 1:04d}.jpg")
                with open(p, "wb") as fp:
                    fp.write(f.jpeg)
        except OSError as e:
            QtWidgets.QMessageBox.critical(self, "导出失败", str(e))
            return
        self._log(f"[批量导出] {len(self._frames)} 个 JPG → {out_dir}")

    # ------------------------------------------------------------------
    def _log(self, msg: str):
        # 复用 main_window 的日志（如果有），否则简单打印
        print(msg)


