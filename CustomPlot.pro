# ============================================================
# 串口数据采集与测控上位机 —— qmake 工程文件
# 构建环境 : Qt 6.8.3 (MSVC2022 64bit) / C++17
# 模块划分 : core(核心逻辑) / utils(工具) / ui(界面) 三个 .pri
# 功能阶段 : S1 骨架+串口收发 / S2 协议解析 / S3 波形 / S4 控制 / S5 闭环
#            S6 数据持久化+历史回放 / S7 统计分析
#            S8 SQLite 数据库 + 登录鉴权 + 角色权限 + 阈值报警
# ============================================================

QT += widgets serialport sql   # widgets: GUI界面; serialport: 串口; sql: SQLite 用户/审计/报警库

CONFIG += c++17

# qmake bug 规避：
# 头文件位于子目录 + 影子构建时，qmake 给 moc 生成的依赖路径会多出两层 ".."，
# Qt Creator 自带的 jom 1.1.6 会严格校验路径而报 "dependent does not exist"。
# 关闭 include 依赖生成即可绕过（副作用：修改 Qt 系统头文件不会触发重编，可接受）。
CONFIG -= depend_includepath

TEMPLATE = app
TARGET = SerialDebugger

# MSVC 下按 UTF-8 解释源文件中的中文字符串，避免乱码
msvc: QMAKE_CXXFLAGS += /utf-8

SOURCES += src/app/main.cpp

include(3rdparty/qcustomplot/3rdparty.pri)
include(src/core/core.pri)
include(src/utils/utils.pri)
include(src/ui/ui.pri)

RESOURCES += resources/resources.qrc

# 默认部署规则
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
