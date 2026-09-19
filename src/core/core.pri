# 核心逻辑层模块：串口 / 协议 / 控制 / 配置 / 存储 / 统计 / 数据库 / 认证 / 报警
# INCLUDEPATH 同时导出子目录，源文件中可直接 #include "SerialManager.h" 形式

INCLUDEPATH += $$PWD $$PWD/serial $$PWD/config $$PWD/protocol $$PWD/control \
               $$PWD/storage $$PWD/statistics $$PWD/db $$PWD/auth $$PWD/alarm

HEADERS += \
    $$PWD/serial/SerialManager.h \
    $$PWD/config/AppSettings.h \
    $$PWD/protocol/Crc8.h \
    $$PWD/protocol/ProtocolDefs.h \
    $$PWD/protocol/ProtocolEngine.h \
    $$PWD/control/ControlRouter.h \
    $$PWD/control/DialController.h \
    $$PWD/control/TempController.h \
    $$PWD/storage/CsvRecorder.h \
    $$PWD/storage/CsvLoader.h \
    $$PWD/storage/LogReplayer.h \
    $$PWD/statistics/StatEngine.h \
    $$PWD/db/DbTime.h \
    $$PWD/db/Database.h \
    $$PWD/db/UserService.h \
    $$PWD/db/AuditService.h \
    $$PWD/db/AlarmStore.h \
    $$PWD/auth/UserInfo.h \
    $$PWD/auth/Session.h \
    $$PWD/alarm/AlarmDefs.h \
    $$PWD/alarm/AlarmEngine.h

SOURCES += \
    $$PWD/serial/SerialManager.cpp \
    $$PWD/config/AppSettings.cpp \
    $$PWD/protocol/Crc8.cpp \
    $$PWD/protocol/ProtocolEngine.cpp \
    $$PWD/control/ControlRouter.cpp \
    $$PWD/control/DialController.cpp \
    $$PWD/control/TempController.cpp \
    $$PWD/storage/CsvRecorder.cpp \
    $$PWD/storage/CsvLoader.cpp \
    $$PWD/storage/LogReplayer.cpp \
    $$PWD/statistics/StatEngine.cpp \
    $$PWD/db/Database.cpp \
    $$PWD/db/UserService.cpp \
    $$PWD/db/AuditService.cpp \
    $$PWD/db/AlarmStore.cpp \
    $$PWD/auth/Session.cpp \
    $$PWD/alarm/AlarmEngine.cpp
