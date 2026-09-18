# 界面层模块：主窗口 + 各面板 + 自定义控件 + 对话框
# INCLUDEPATH 导出 panels / widgets / dialogs 子目录，源文件中可直接 #include "SerialPanel.h"

INCLUDEPATH += $$PWD $$PWD/panels $$PWD/widgets $$PWD/dialogs

HEADERS += \
    $$PWD/MainWindow.h \
    $$PWD/panels/SerialPanel.h \
    $$PWD/panels/DiagPanel.h \
    $$PWD/panels/WavePanel.h \
    $$PWD/panels/DataPanel.h \
    $$PWD/panels/ControlPanel.h \
    $$PWD/panels/StatPanel.h \
    $$PWD/panels/LogPanel.h \
    $$PWD/panels/AlarmPanel.h \
    $$PWD/widgets/LedIndicator.h \
    $$PWD/dialogs/LoginDialog.h \
    $$PWD/dialogs/UserManagerDialog.h

SOURCES += \
    $$PWD/MainWindow.cpp \
    $$PWD/panels/SerialPanel.cpp \
    $$PWD/panels/DiagPanel.cpp \
    $$PWD/panels/WavePanel.cpp \
    $$PWD/panels/DataPanel.cpp \
    $$PWD/panels/ControlPanel.cpp \
    $$PWD/panels/StatPanel.cpp \
    $$PWD/panels/LogPanel.cpp \
    $$PWD/panels/AlarmPanel.cpp \
    $$PWD/widgets/LedIndicator.cpp \
    $$PWD/dialogs/LoginDialog.cpp \
    $$PWD/dialogs/UserManagerDialog.cpp
