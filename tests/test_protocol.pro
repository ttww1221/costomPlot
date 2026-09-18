# ============================================================
# 核心层单元测试工程（独立于主工程，可在 Qt Creator 单独打开运行）
# 依赖：QtCore + QtSql + QtTest，直接编译被测源文件，不引入界面模块
# 覆盖：协议解析 / 指令路由 / 温度闭环 / CSV存储回放(S6) / 统计引擎(S7)
#       / 数据库与用户鉴权(S8) / 报警引擎(S8)
# ============================================================

QT += core sql testlib

CONFIG += c++17 console
CONFIG -= app_bundle
# 与主工程保持一致的 qmake bug 规避
CONFIG -= depend_includepath

TEMPLATE = app
TARGET = test_protocol

# MSVC 下按 UTF-8 解释源文件中的中文字符串
msvc: QMAKE_CXXFLAGS += /utf-8

# 被测代码直接以源文件形式编入测试工程
INCLUDEPATH += ../src/core ../src/core/protocol ../src/core/serial ../src/core/config \
               ../src/core/control ../src/core/storage ../src/core/statistics \
               ../src/core/db ../src/core/auth ../src/core/alarm

SOURCES += \
    test_protocol.cpp \
    ../src/core/protocol/ProtocolEngine.cpp \
    ../src/core/protocol/Crc8.cpp \
    ../src/core/control/ControlRouter.cpp \
    ../src/core/control/TempController.cpp \
    ../src/core/storage/CsvRecorder.cpp \
    ../src/core/storage/CsvLoader.cpp \
    ../src/core/storage/LogReplayer.cpp \
    ../src/core/statistics/StatEngine.cpp \
    ../src/core/db/Database.cpp \
    ../src/core/db/UserService.cpp \
    ../src/core/db/AuditService.cpp \
    ../src/core/db/AlarmStore.cpp \
    ../src/core/auth/Session.cpp \
    ../src/core/alarm/AlarmEngine.cpp

HEADERS += \
    ../src/core/protocol/ProtocolEngine.h \
    ../src/core/protocol/Crc8.h \
    ../src/core/protocol/ProtocolDefs.h \
    ../src/core/control/ControlRouter.h \
    ../src/core/control/TempController.h \
    ../src/core/storage/CsvRecorder.h \
    ../src/core/storage/CsvLoader.h \
    ../src/core/storage/LogReplayer.h \
    ../src/core/statistics/StatEngine.h \
    ../src/core/db/DbTime.h \
    ../src/core/db/Database.h \
    ../src/core/db/UserService.h \
    ../src/core/db/AuditService.h \
    ../src/core/db/AlarmStore.h \
    ../src/core/auth/UserInfo.h \
    ../src/core/auth/Session.h \
    ../src/core/alarm/AlarmDefs.h \
    ../src/core/alarm/AlarmEngine.h
