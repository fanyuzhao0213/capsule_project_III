"""
main.py
TYGD31 接收显示工具入口

功能：
  - Tab 1 (实时): 直接接 STM32 UART3，实时显示 JPEG 图像
  - Tab 2 (U盘): 浏览 SD 卡上的 Y<SN>.YS 文件，按帧解码

依赖安装：
  pip install PyQt5 pyserial

运行：
  python main.py
"""

from __future__ import annotations
import sys
from PyQt5 import QtCore, QtGui, QtWidgets

from serial_viewer import SerialViewer
from file_viewer import FileViewer


APP_NAME = "ZASY胶囊系统图像显示系统"
APP_VERSION = "1.0.0"


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} v{APP_VERSION}")
        self.resize(1200, 800)

        # 中央 Tab
        self.tabs = QtWidgets.QTabWidget()
        self.tab_serial = SerialViewer()
        self.tab_file = FileViewer()
        self.tabs.addTab(self.tab_serial, "实时串口 (UART3)")
        self.tabs.addTab(self.tab_file, "U盘文件 (.YS)")

        self.setCentralWidget(self.tabs)

        # 全局日志 dock：收集两个 tab 的 _log 调用
        self.log_dock = QtWidgets.QDockWidget("事件日志", self)
        self.txt_log = QtWidgets.QPlainTextEdit()
        self.txt_log.setReadOnly(True)
        self.log_dock.setWidget(self.txt_log)
        self.addDockWidget(QtCore.Qt.BottomDockWidgetArea, self.log_dock)
        # 把两个 tab 的 _log 桥接到这里
        self.tab_serial._log = self._bridge_log(self.tab_serial._log, "[串口] ")
        self.tab_file._log = self._bridge_log(self.tab_file._log, "[U盘] ")

        # 状态栏
        self.statusBar().showMessage("就绪")

    def _bridge_log(self, original, prefix: str):
        def wrapped(msg: str):
            original(msg)
            from datetime import datetime
            ts = datetime.now().strftime("%H:%M:%S")
            self.txt_log.appendPlainText(f"[{ts}] {prefix}{msg}")
        return wrapped


def main():
    QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
    app = QtWidgets.QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    win = MainWindow()
    win.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()

