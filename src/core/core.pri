# 核心逻辑层模块：串口管理 / 协议解析 / 配置管理
# INCLUDEPATH 同时导出子目录，源文件中可直接 #include "SerialManager.h" 形式

INCLUDEPATH += $$PWD $$PWD/serial $$PWD/config $$PWD/protocol

HEADERS += \
    $$PWD/serial/SerialManager.h \
    $$PWD/config/AppSettings.h \
    $$PWD/protocol/Crc8.h \
    $$PWD/protocol/ProtocolEngine.h

SOURCES += \
    $$PWD/serial/SerialManager.cpp \
    $$PWD/config/AppSettings.cpp \
    $$PWD/protocol/Crc8.cpp \
    $$PWD/protocol/ProtocolEngine.cpp
