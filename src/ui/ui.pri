# 界面层模块：主窗口 + 各面板 + 自定义控件
# INCLUDEPATH 导出 panels / widgets 子目录，源文件中可直接 #include "SerialPanel.h"

INCLUDEPATH += $$PWD $$PWD/panels $$PWD/widgets

HEADERS += \
    $$PWD/MainWindow.h \
    $$PWD/panels/SerialPanel.h \
    $$PWD/panels/DiagPanel.h \
    $$PWD/panels/WavePanel.h \
    $$PWD/panels/DataPanel.h \
    $$PWD/widgets/LedIndicator.h

SOURCES += \
    $$PWD/MainWindow.cpp \
    $$PWD/panels/SerialPanel.cpp \
    $$PWD/panels/DiagPanel.cpp \
    $$PWD/panels/WavePanel.cpp \
    $$PWD/panels/DataPanel.cpp \
    $$PWD/widgets/LedIndicator.cpp
