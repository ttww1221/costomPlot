# ============================================================
# 协议层单元测试工程（独立于主工程，可在 Qt Creator 单独打开运行）
# 依赖：QtCore + QtTest，直接编译被测源文件，不引入界面模块
# ============================================================

QT += core testlib

CONFIG += c++17 console
CONFIG -= app_bundle
# 与主工程保持一致的 qmake bug 规避
CONFIG -= depend_includepath

TEMPLATE = app
TARGET = test_protocol

# MSVC 下按 UTF-8 解释源文件中的中文字符串
msvc: QMAKE_CXXFLAGS += /utf-8

# 被测代码直接以源文件形式编入测试工程
INCLUDEPATH += ../src/core ../src/core/protocol ../src/core/serial ../src/core/config

SOURCES += \
    test_protocol.cpp \
    ../src/core/protocol/ProtocolEngine.cpp \
    ../src/core/protocol/Crc8.cpp

HEADERS += \
    ../src/core/protocol/ProtocolEngine.h \
    ../src/core/protocol/Crc8.h
