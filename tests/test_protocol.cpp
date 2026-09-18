/**
 * @file    test_protocol.cpp
 * @brief   核心层单元测试（QTest）
 *
 * 覆盖内容：
 *  - CRC-8-ATM 已知向量校验（设计文档 3.2 节示例）
 *  - 完整帧 / 逐字节断帧 / 粘包多帧 / 噪声前缀 解析
 *  - 坏 CRC 丢弃计数、帧尾失配重同步、载荷内 0xAA 干扰免疫
 *  - ACK 应答解析、指令路由排队与超时重发（S4）
 *  - 滞环单边温控、手动模式静默、ACK 状态同步（S5）
 *  - CSV 记录 → 装载 往返一致性、非法行容错、多列格式兼容（S6）
 *  - 历史回放：帧序、时间戳重写、进度上报、CSV 直载（S6）
 *  - Welford 均值/标准差、极值时刻、分布直方图、丢包率、速率窗口（S7）
 *  - 口令加盐迭代哈希、账号 CRUD 校验、唯一管理员保护、登录鉴权（S8）
 *  - 操作审计写入/过滤查询、报警事件入库与确认流程（S8）
 *  - 报警去抖状态机、分级判据、上下限与湿度通道、非法阈值拒绝（S8）
 *  - 会话登录/注销、角色权限矩阵、权限变化广播（S8）
 *
 * 运行方式：Qt Creator 打开 tests/test_protocol.pro 直接运行，
 * 或命令行 nmake 后执行 build 目录下的 test_protocol.exe。
 */

#include <QtTest>

#include <cmath>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "AlarmEngine.h"
#include "AlarmStore.h"
#include "AuditService.h"
#include "Crc8.h"
#include "CsvLoader.h"
#include "CsvRecorder.h"
#include "ControlRouter.h"
#include "Database.h"
#include "LogReplayer.h"
#include "ProtocolEngine.h"
#include "Session.h"
#include "StatEngine.h"
#include "TempController.h"
#include "UserInfo.h"
#include "UserService.h"

// ---------------------------------------------------------------------------
// 测试辅助：按协议组装一帧
// ---------------------------------------------------------------------------

/**
 * @brief 组装一条完整上行帧（AA BB + 温度 + 湿度 + CRC + 0D 0A）
 * @param badCrc    为 true 时故意破坏 CRC，用于测试坏帧丢弃
 * @param badTail   为 true 时把帧尾 0D 0A 改成 0E 0B，用于测试失步
 */
static QByteArray makeFrame(float temp, float hum, bool badCrc = false, bool badTail = false)
{
    QByteArray frame;
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0xBB));
    // 温度 4 字节（小端）
    for (int i = 0; i < 4; ++i)
        frame.append(reinterpret_cast<const char *>(&temp)[i]);
    // 湿度 4 字节（小端）
    for (int i = 0; i < 4; ++i)
        frame.append(reinterpret_cast<const char *>(&hum)[i]);
    // CRC-8-ATM（对载荷 8 字节）
    uint8_t crc = Crc8::compute(reinterpret_cast<const uint8_t *>(frame.constData() + 2), 8);
    if (badCrc)
        crc ^= 0xFF;  // 翻转 CRC
    frame.append(static_cast<char>(crc));
    // 帧尾
    frame.append(static_cast<char>(badTail ? 0x0E : 0x0D));
    frame.append(static_cast<char>(badTail ? 0x0B : 0x0A));
    return frame;
}

// ---------------------------------------------------------------------------
// TestCrc：CRC-8-ATM 算法正确性
// ---------------------------------------------------------------------------

class TestCrc : public QObject
{
    Q_OBJECT

private slots:
    // 文档示例载荷 CD CC CC 41 33 33 79 42 的 CRC。
    // 注意：文档 3.2 节示例字节写的是 0xA3，经两种独立实现验证实际为 0x36，
    // 文档示例值与其声明的算法参数（poly 0x07 / init 0x00）不符，以算法参数为准。
    void knownVector();
    // 标准检查值：RevEng CRC 目录 CRC-8(poly 0x07) 对 "123456789" 的 check = 0xF4
    void standardCheckValue();
    // 空数据：CRC 保持初始值 0x00
    void emptyData();
    // 单字节 0x00：无高位 1 触发，结果应为 0x00
    void singleByte();
};

void TestCrc::knownVector()
{
    const QByteArray payload = QByteArray::fromHex("CDCCCC4133337942");
    QCOMPARE(Crc8::compute(reinterpret_cast<const uint8_t *>(payload.constData()),
                           static_cast<size_t>(payload.size())),
             static_cast<uint8_t>(0x36));
}

void TestCrc::standardCheckValue()
{
    const QByteArray data = QByteArray::fromHex("313233343536373839");  // "123456789"
    QCOMPARE(Crc8::compute(reinterpret_cast<const uint8_t *>(data.constData()),
                           static_cast<size_t>(data.size())),
             static_cast<uint8_t>(0xF4));
}

void TestCrc::emptyData()
{
    const uint8_t dummy = 0;
    QCOMPARE(Crc8::compute(&dummy, 0), static_cast<uint8_t>(0x00));
}

void TestCrc::singleByte()
{
    // 单字节 0x00 的 CRC：经过 8 次移位，无高位 1，结果应为 0x00
    const uint8_t data[] = {0x00};
    QCOMPARE(Crc8::compute(data, 1), static_cast<uint8_t>(0x00));
}

// ---------------------------------------------------------------------------
// TestProtocol：帧同步状态机
// ---------------------------------------------------------------------------

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void singleValidFrame();      // 整帧一次到达 → 解析出 1 帧，数值正确
    void fragmentedFrame();       // 逐字节到达 → 仍解析出 1 帧（抗断帧）
    void concatenatedFrames();    // 两帧粘连一次到达 → 解析出 2 帧（抗粘包）
    void garbageBeforeFrame();    // 噪声前缀 → 正常解析，不误计失步
    void badCrcDiscard();         // CRC 破坏 → 丢弃并计数
    void badTailResync();         // 帧尾错误 → 失步计数，后续帧仍可解析
    void aaBbInPayload();         // 载荷中出现 AA BB → 不干扰解析（按长度收取）
    void trailingGarbage();       // 帧后跟垃圾字节 → 帧正常，垃圾忽略
    void ackAfterDataFrame();     // 数据帧后紧跟ACK → 两者都正确解析
    void dataFrameBbNotMisreadAsAck(); // 数据帧头里的BB不触发ACK
    void ackWithUnknownCmdIgnored();   // BB + 非法指令码 → 忽略
};

void TestProtocol::singleValidFrame()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 25.6°C / 62.3% —— 与设计文档示例一致
    const QByteArray frame = makeFrame(25.6f, 62.3f);
    engine.slotFeed(frame);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const SensorData data = args.at(0).value<SensorData>();
    QVERIFY(qAbs(data.temperature - 25.6) < 0.01);
    QVERIFY(qAbs(data.humidity - 62.3) < 0.01);
    QCOMPARE(data.frameIndex, qint64(1));
}

void TestProtocol::fragmentedFrame()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    const QByteArray frame = makeFrame(25.6f, 62.3f);
    // 逐字节喂入，模拟串口 readyRead 每次只到 1 字节的极端情况
    for (char c : frame)
        engine.slotFeed(QByteArray(1, c));

    QCOMPARE(spy.count(), 1);
}

void TestProtocol::concatenatedFrames()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 两帧直接首尾相连，一次到达（粘包）
    QByteArray stream = makeFrame(20.0f, 50.0f) + makeFrame(21.0f, 51.0f);
    engine.slotFeed(stream);

    QCOMPARE(spy.count(), 2);
    const SensorData first = spy.at(0).at(0).value<SensorData>();
    const SensorData second = spy.at(1).at(0).value<SensorData>();
    QVERIFY(qAbs(first.temperature - 20.0) < 0.01);
    QVERIFY(qAbs(second.temperature - 21.0) < 0.01);
}

void TestProtocol::garbageBeforeFrame()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 帧前混入 20 字节随机噪声（不含 AA 开头的序列干扰状态机）
    QByteArray noise;
    for (int i = 0; i < 20; ++i)
        noise.append(static_cast<char>(0x10 + i));
    engine.slotFeed(noise + makeFrame(25.6f, 62.3f));

    QCOMPARE(spy.count(), 1);
    // 噪声位于同步之前，不应计入失步
    QCOMPARE(engine.stats().syncLosses, qint64(0));
}

void TestProtocol::badCrcDiscard()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    engine.slotFeed(makeFrame(25.6f, 62.3f, /*badCrc=*/true));

    QCOMPARE(spy.count(), 0);                         // 无有效帧输出
    QCOMPARE(engine.stats().totalFrames, qint64(1));  // 帧数已计
    QCOMPARE(engine.stats().crcErrors, qint64(1));    // CRC 错误 +1
}

void TestProtocol::badTailResync()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 第一帧帧尾错误（0E 0B），第二帧正常 —— 验证失步后能恢复同步
    engine.slotFeed(makeFrame(25.6f, 62.3f, false, /*badTail=*/true)
                    + makeFrame(25.7f, 62.4f));

    QCOMPARE(spy.count(), 1);  // 只有第二帧被解析
    QVERIFY(engine.stats().syncLosses >= 1);
    const SensorData data = spy.at(0).at(0).value<SensorData>();
    QVERIFY(qAbs(data.temperature - 25.7) < 0.01);
}

void TestProtocol::aaBbInPayload()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 手工构造：温度浮点小端字节故意以 AA BB 开头（-1.403e-38 之类），
    // 载荷中 0xAA 0xBB 不应被误认为新帧头
    QByteArray frame;
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0xBB));
    frame.append(static_cast<char>(0xAA));  // 温度字节0 = 0xAA
    frame.append(static_cast<char>(0xBB));  // 温度字节1 = 0xBB
    frame.append(static_cast<char>(0xCC));
    frame.append(static_cast<char>(0xCC));
    frame.append(static_cast<char>(0x33));
    frame.append(static_cast<char>(0x33));
    frame.append(static_cast<char>(0x79));
    frame.append(static_cast<char>(0x42));
    const uint8_t crc = Crc8::compute(reinterpret_cast<const uint8_t *>(frame.constData() + 2), 8);
    frame.append(static_cast<char>(crc));
    frame.append(static_cast<char>(0x0D));
    frame.append(static_cast<char>(0x0A));

    engine.slotFeed(frame);

    QCOMPARE(spy.count(), 1);  // 一帧完整解析，无拆帧误判
    QCOMPARE(engine.stats().syncLosses, qint64(0));
}

void TestProtocol::trailingGarbage()
{
    ProtocolEngine engine;
    QSignalSpy spy(&engine, &ProtocolEngine::sigFrameParsed);

    // 帧后跟垃圾字节：帧正常解析，垃圾在搜索态被忽略
    QByteArray garbage;
    for (int i = 0; i < 10; ++i)
        garbage.append(static_cast<char>(0xE0 + i));
    engine.slotFeed(makeFrame(25.6f, 62.3f) + garbage);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(engine.stats().syncLosses, qint64(0));
}

void TestProtocol::ackAfterDataFrame()
{
    ProtocolEngine engine;
    QSignalSpy frameSpy(&engine, &ProtocolEngine::sigFrameParsed);
    QSignalSpy ackSpy(&engine, &ProtocolEngine::sigAckReceived);

    // 数据帧后紧跟风扇指令的 ACK（BB 12 01）
    QByteArray ack;
    ack.append(static_cast<char>(0xBB));
    ack.append(static_cast<char>(0x12));
    ack.append(static_cast<char>(0x01));
    engine.slotFeed(makeFrame(25.6f, 62.3f) + ack);

    QCOMPARE(frameSpy.count(), 1);
    QCOMPARE(ackSpy.count(), 1);
    QCOMPARE(ackSpy.at(0).at(0).value<quint8>(), static_cast<quint8>(0x12));
    QCOMPARE(ackSpy.at(0).at(1).value<quint8>(), static_cast<quint8>(0x01));
}

void TestProtocol::dataFrameBbNotMisreadAsAck()
{
    ProtocolEngine engine;
    QSignalSpy ackSpy(&engine, &ProtocolEngine::sigAckReceived);

    // 数据帧帧头的第2字节BB在SeekBB态被消费，不应触发ACK
    engine.slotFeed(makeFrame(25.6f, 62.3f));
    QCOMPARE(ackSpy.count(), 0);
}

void TestProtocol::ackWithUnknownCmdIgnored()
{
    ProtocolEngine engine;
    QSignalSpy ackSpy(&engine, &ProtocolEngine::sigAckReceived);

    // BB + 非法指令码 0x99 → 视为噪声丢弃
    QByteArray noise;
    noise.append(static_cast<char>(0xBB));
    noise.append(static_cast<char>(0x99));
    noise.append(static_cast<char>(0x01));
    engine.slotFeed(noise);
    QCOMPARE(ackSpy.count(), 0);
}

// ---------------------------------------------------------------------------
// TestControlRouter：指令路由 / ACK 匹配 / 超时重发
// ---------------------------------------------------------------------------

class TestControlRouter : public QObject
{
    Q_OBJECT

private slots:
    void commandAndAck();    // 发送 → ACK匹配 → sigAcked，帧内容正确
    void queueSerializes();  // 等待ACK期间新指令排队，确认后依次发送
    void timeoutRetries();   // 无ACK → 共发3次 → sigTimeout（需事件循环，约1.5s）
};

void TestControlRouter::commandAndAck()
{
    ControlRouter router;
    QSignalSpy sendSpy(&router, &ControlRouter::sigSendFrame);
    QSignalSpy ackSpy(&router, &ControlRouter::sigAcked);

    router.slotSendCommand(0x12, 0x01);
    QCOMPARE(sendSpy.count(), 1);
    QCOMPARE(sendSpy.at(0).at(0).value<QByteArray>(), QByteArray::fromHex("1201"));

    router.slotAckReceived(0x12, 0x01);
    QCOMPARE(ackSpy.count(), 1);
    QCOMPARE(router.pendingCount(), 0);
}

void TestControlRouter::queueSerializes()
{
    ControlRouter router;
    QSignalSpy sendSpy(&router, &ControlRouter::sigSendFrame);

    // 第一条未确认时再入队两条：应只发出第一条
    router.slotSendCommand(0x12, 0x01);
    router.slotSendCommand(0x10, 0x5A);
    router.slotSendCommand(0x30, 0x00);
    QCOMPARE(sendSpy.count(), 1);
    QCOMPARE(router.pendingCount(), 3);

    // 确认后自动发出下一条
    router.slotAckReceived(0x12, 0x01);
    QCOMPARE(sendSpy.count(), 2);
    QCOMPARE(sendSpy.at(1).at(0).value<QByteArray>(), QByteArray::fromHex("105A"));
}

void TestControlRouter::timeoutRetries()
{
    ControlRouter router;
    QSignalSpy sendSpy(&router, &ControlRouter::sigSendFrame);
    QSignalSpy timeoutSpy(&router, &ControlRouter::sigTimeout);

    router.slotSendCommand(0x30, 0x00);
    // 首发500ms超时→重试1→500ms超时→重试2→500ms超时→放弃，共约1.5s
    QVERIFY(timeoutSpy.wait(2500));
    QCOMPARE(sendSpy.count(), 3);   // 首发 + 2次重试 = 3次发送
    QCOMPARE(timeoutSpy.at(0).at(0).value<quint8>(), static_cast<quint8>(0x30));
}

// ---------------------------------------------------------------------------
// TestTempController：滞环单边温控（S5）
// ---------------------------------------------------------------------------

class TestTempController : public QObject
{
    Q_OBJECT

private slots:
    void hysteresisSwitch();  // 越上界开、死区保持、越下界关
    void disabledNoOutput();  // 手动模式不输出风扇指令
    void ackSyncNoSpam();     // ACK同步后进入死区不发重复指令
};

// 构造一帧只带温度的传感器数据
static SensorData makeSensor(double temp)
{
    SensorData d;
    d.temperature = temp;
    d.humidity = 50.0;
    d.timestamp = QDateTime::currentDateTime();
    return d;
}

void TestTempController::hysteresisSwitch()
{
    TempController ctrl;
    ctrl.setEnabled(true);
    ctrl.setTarget(25.0);   // 滞环默认0.5：上界25.5 / 下界24.5
    QSignalSpy spy(&ctrl, &TempController::sigFanCommand);

    ctrl.slotSensorData(makeSensor(26.0));   // > 25.5 → 开
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<bool>(), true);

    ctrl.slotSensorData(makeSensor(25.2));   // 死区内 → 保持开，无新指令
    QCOMPARE(spy.count(), 1);

    ctrl.slotSensorData(makeSensor(24.0));   // < 24.5 → 关
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).value<bool>(), false);

    ctrl.slotSensorData(makeSensor(24.4));   // 死区内 → 保持关，无新指令
    QCOMPARE(spy.count(), 2);

    ctrl.slotSensorData(makeSensor(26.0));   // 再次越上界 → 开
    QCOMPARE(spy.count(), 3);
}

void TestTempController::disabledNoOutput()
{
    TempController ctrl;   // 默认手动模式
    ctrl.setTarget(25.0);
    QSignalSpy spy(&ctrl, &TempController::sigFanCommand);

    ctrl.slotSensorData(makeSensor(30.0));   // 远超上界也不应输出
    ctrl.slotSensorData(makeSensor(20.0));
    QCOMPARE(spy.count(), 0);
}

void TestTempController::ackSyncNoSpam()
{
    TempController ctrl;
    ctrl.setEnabled(true);
    ctrl.setTarget(25.0);
    QSignalSpy spy(&ctrl, &TempController::sigFanCommand);

    // 手动模式遗留：风扇实际是开的，ACK同步给控制器
    ctrl.slotFanStateConfirmed(true);

    ctrl.slotSensorData(makeSensor(24.0));   // 低于下界 → 应发"关"
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<bool>(), false);

    ctrl.slotSensorData(makeSensor(24.4));   // 死区 → 不再发
    QCOMPARE(spy.count(), 1);
}

// ---------------------------------------------------------------------------
// 测试辅助：构造带完整字段的传感器帧
// ---------------------------------------------------------------------------

static SensorData makeTimedSensor(double temp, double hum, const QDateTime &ts, qint64 idx)
{
    SensorData d;
    d.temperature = temp;
    d.humidity = hum;
    d.timestamp = ts;
    d.frameIndex = idx;
    return d;
}

// 在临时目录里手写一个 CSV 文件（用于容错与格式兼容测试）
static QString writeTempCsv(const QString &dirPath, const QString &name, const QStringList &lines)
{
    const QString path = QDir(dirPath).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    QTextStream ts(&f);
    for (const QString &l : lines)
        ts << l << '\n';
    f.close();
    return path;
}

// ---------------------------------------------------------------------------
// TestCsvStorage：CSV 记录与装载（S6）
// ---------------------------------------------------------------------------

class TestCsvStorage : public QObject
{
    Q_OBJECT

private slots:
    void recordThenLoadRoundTrip();  // 记录3帧→重新装载，数值/时间戳/序号一致
    void recordWithoutStartNoop();   // 未开始记录时写入不产生任何副作用
    void threeColumnCompatible();    // 兼容无帧序号的3列写法
    void badLinesSkipped();          // 非法行被跳过并计数，不中断装载
};

void TestCsvStorage::recordThenLoadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    CsvRecorder rec;
    QSignalSpy startSpy(&rec, &CsvRecorder::sigStarted);
    QVERIFY(rec.start(dir.path()));
    QCOMPARE(startSpy.count(), 1);

    const QString path = startSpy.at(0).at(0).toString();
    QVERIFY(QFile::exists(path));
    QVERIFY(path.endsWith(QStringLiteral(".csv")));

    // 模拟下位机 200ms 一帧的采样节奏
    const QDateTime base(QDate(2026, 9, 17), QTime(15, 30, 22, 123));
    for (int i = 0; i < 3; ++i)
        rec.slotRecord(makeTimedSensor(25.5 + i, 60.25 + i, base.addMSecs(200 * i), i + 1));
    QCOMPARE(rec.rowCount(), qint64(3));

    QSignalSpy stopSpy(&rec, &CsvRecorder::sigStopped);
    rec.stop();
    QCOMPARE(stopSpy.count(), 1);
    QCOMPARE(stopSpy.at(0).at(1).value<qint64>(), qint64(3));
    QVERIFY(!rec.isRecording());

    // 重新装载：数值精度按 CSV 的 2 位小数写入口径比对
    const CsvLoader::Result res = CsvLoader::load(path);
    QVERIFY(res.ok);
    QVERIFY(res.error.isEmpty());
    QCOMPARE(res.badLines, 0);
    QCOMPARE(res.frames.size(), 3);

    for (int i = 0; i < 3; ++i) {
        QCOMPARE(res.frames.at(i).frameIndex, qint64(i + 1));
        QCOMPARE(res.frames.at(i).timestamp, base.addMSecs(200 * i));
        QVERIFY(qAbs(res.frames.at(i).temperature - (25.5 + i)) < 0.005);
        QVERIFY(qAbs(res.frames.at(i).humidity - (60.25 + i)) < 0.005);
    }
}

void TestCsvStorage::recordWithoutStartNoop()
{
    CsvRecorder rec;
    QSignalSpy progressSpy(&rec, &CsvRecorder::sigProgress);

    rec.slotRecord(makeTimedSensor(25.0, 60.0, QDateTime::currentDateTime(), 1));

    QCOMPARE(rec.rowCount(), qint64(0));
    QVERIFY(rec.filePath().isEmpty());
    QCOMPARE(progressSpy.count(), 0);

    // 未记录时 stop() 也不应发出 sigStopped
    QSignalSpy stopSpy(&rec, &CsvRecorder::sigStopped);
    rec.stop();
    QCOMPARE(stopSpy.count(), 0);
}

void TestCsvStorage::threeColumnCompatible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writeTempCsv(dir.path(), QStringLiteral("manual.csv"), QStringList{
        QStringLiteral("timestamp,temperature,humidity"),
        QStringLiteral("2026-09-17 15:30:22.000,25.50,60.00"),
        QStringLiteral("2026-09-17 15:30:22.200,25.70,60.50"),
    });
    QVERIFY(!path.isEmpty());

    const CsvLoader::Result res = CsvLoader::load(path);
    QVERIFY(res.ok);
    QCOMPARE(res.frames.size(), 2);
    QCOMPARE(res.badLines, 0);
    // 3列格式没有帧序号字段，应回落为 0 而不是解析失败
    QCOMPARE(res.frames.at(0).frameIndex, qint64(0));
    QVERIFY(qAbs(res.frames.at(1).temperature - 25.70) < 1e-9);
}

void TestCsvStorage::badLinesSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = writeTempCsv(dir.path(), QStringLiteral("broken.csv"), QStringList{
        QStringLiteral("timestamp,frame_index,temperature,humidity"),
        QStringLiteral("2026-09-17 15:30:22.000,1,25.50,60.00"),
        QStringLiteral("这一行是手工误输入的中文说明"),
        QStringLiteral("2026-09-17 15:30:22.200,2,notanumber,60.00"),
        QString(),   // 空行不计入错误
        QStringLiteral("2026-09-17 15:30:22.400,3,25.90,61.00"),
    });
    QVERIFY(!path.isEmpty());

    const CsvLoader::Result res = CsvLoader::load(path);
    QVERIFY(res.ok);
    QCOMPARE(res.frames.size(), 2);          // 只有两行合法
    QCOMPARE(res.badLines, 2);               // 中文说明行 + 数值非法行
    QCOMPARE(res.frames.at(0).frameIndex, qint64(1));
    QCOMPARE(res.frames.at(1).frameIndex, qint64(3));

    // 不存在的文件：ok=false 且给出原因
    const CsvLoader::Result missing = CsvLoader::load(QDir(dir.path()).filePath("nope.csv"));
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.isEmpty());
}

// ---------------------------------------------------------------------------
// TestLogReplayer：历史数据回放（S6）
// ---------------------------------------------------------------------------

class TestLogReplayer : public QObject
{
    Q_OBJECT

private slots:
    void playbackRestampsAndFinishes(); // 顺序播放、时间戳重写为当前时刻、结束信号
    void seekReportsOriginalTime();     // 定位后上报原始录制时间
    void loadFromRecordedCsv();         // 由记录器产出的 CSV 直接载入并回放
    void emptySourceRefusesPlay();      // 无数据时拒绝播放并提示
};

void TestLogReplayer::playbackRestampsAndFinishes()
{
    LogReplayer rp;
    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);

    QVector<SensorData> frames;
    for (int i = 0; i < 3; ++i)
        frames.append(makeTimedSensor(20.0 + i, 50.0, base.addMSecs(200 * i), i + 1));
    rp.setFrames(frames);
    QCOMPARE(rp.frameCount(), 3);
    QCOMPARE(rp.originalTimeAt(1), base.addMSecs(200));

    rp.setSpeed(10.0);   // 10倍速：200ms 帧间隔压缩为 20ms，测试不必久等
    QSignalSpy frameSpy(&rp, &LogReplayer::sigFrame);
    QSignalSpy finSpy(&rp, &LogReplayer::sigFinished);

    rp.slotPlay();
    QVERIFY(rp.isPlaying());
    QVERIFY(finSpy.wait(3000));
    QCOMPARE(frameSpy.count(), 3);
    QVERIFY(!rp.isPlaying());

    for (int i = 0; i < 3; ++i) {
        const SensorData d = frameSpy.at(i).at(0).value<SensorData>();
        QCOMPARE(d.frameIndex, qint64(i + 1));
        QVERIFY(qAbs(d.temperature - (20.0 + i)) < 1e-9);
        // 时间戳被重写为"当前时刻"，否则会被波形面板的滑动窗口立即裁掉
        QVERIFY(d.timestamp.msecsTo(QDateTime::currentDateTime()) < 5000);
        QVERIFY(d.timestamp > base.addSecs(3000));
    }
}

void TestLogReplayer::seekReportsOriginalTime()
{
    LogReplayer rp;
    const QDateTime base(QDate(2026, 9, 17), QTime(8, 0, 0, 0));

    QVector<SensorData> frames;
    for (int i = 0; i < 5; ++i)
        frames.append(makeTimedSensor(22.0 + i, 55.0, base.addMSecs(200 * i), i + 1));
    rp.setFrames(frames);

    QSignalSpy progSpy(&rp, &LogReplayer::sigProgress);
    rp.slotSeek(3);
    QCOMPARE(progSpy.count(), 1);
    QCOMPARE(progSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(progSpy.at(0).at(1).toInt(), 5);
    QCOMPARE(progSpy.at(0).at(2).value<QDateTime>(), base.addMSecs(600));
    QCOMPARE(rp.currentIndex(), 3);

    // 越界 seek 被夹紧到合法区间
    rp.slotSeek(999);
    QCOMPARE(rp.currentIndex(), 4);
    rp.slotSeek(-5);
    QCOMPARE(rp.currentIndex(), 0);
}

void TestLogReplayer::loadFromRecordedCsv()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    CsvRecorder rec;
    QVERIFY(rec.start(dir.path()));
    const QString path = rec.filePath();
    const QDateTime base = QDateTime::currentDateTime().addSecs(-60);
    for (int i = 0; i < 4; ++i)
        rec.slotRecord(makeTimedSensor(24.0 + i * 0.5, 58.0, base.addMSecs(200 * i), i + 1));
    rec.stop();

    LogReplayer rp;
    QSignalSpy loadSpy(&rp, &LogReplayer::sigLoaded);
    QVERIFY(rp.load(path));
    QCOMPARE(loadSpy.count(), 1);
    QCOMPARE(rp.frameCount(), 4);
    QVERIFY(!rp.isPlaying());

    rp.setSpeed(10.0);
    QSignalSpy frameSpy(&rp, &LogReplayer::sigFrame);
    QSignalSpy finSpy(&rp, &LogReplayer::sigFinished);
    rp.slotPlay();
    QVERIFY(finSpy.wait(3000));
    QCOMPARE(frameSpy.count(), 4);
    QVERIFY(qAbs(frameSpy.at(3).at(0).value<SensorData>().temperature - 25.5) < 0.005);
}

void TestLogReplayer::emptySourceRefusesPlay()
{
    LogReplayer rp;
    QSignalSpy msgSpy(&rp, &LogReplayer::sigMessage);
    QSignalSpy frameSpy(&rp, &LogReplayer::sigFrame);

    rp.slotPlay();
    QCOMPARE(frameSpy.count(), 0);
    QCOMPARE(msgSpy.count(), 1);
    QVERIFY(!rp.isPlaying());
}

// ---------------------------------------------------------------------------
// TestStatEngine：统计分析（S7）
// ---------------------------------------------------------------------------

class TestStatEngine : public QObject
{
    Q_OBJECT

private slots:
    void welfordMeanAndStddev();     // Welford 结果与离线公式一致（含大均值稳定性）
    void minMaxWithTimestamp();      // 极值及其出现时刻
    void histogramCoversAllSamples();// 直方图各箱计数之和等于样本数
    void constantValueHistogram();   // 全部同值时区间被撑开，不除零
    void lossRateFromProtoStats();   // 丢包率 = (CRC错误+失步)/总帧数
    void rateWindowAndReset();       // 1s 窗口的速率/帧率结算与一键清零
};

// 把一组温度值依次喂入统计引擎（湿度固定，避免干扰）
static void feedTemps(StatEngine &e, const QVector<double> &temps)
{
    const QDateTime base = QDateTime::currentDateTime();
    for (int i = 0; i < temps.size(); ++i)
        e.slotSample(makeTimedSensor(temps.at(i), 50.0, base.addMSecs(200 * i), i + 1));
}

void TestStatEngine::welfordMeanAndStddev()
{
    StatEngine e;
    feedTemps(e, {10.0, 12.0, 14.0, 16.0, 18.0});

    const ChannelStat t = e.snapshot().temp;
    QCOMPARE(t.count, qint64(5));
    QVERIFY(qAbs(t.mean - 14.0) < 1e-12);
    // 样本标准差（n-1）：sqrt(40/4) = sqrt(10)
    QVERIFY(qAbs(t.stddev - std::sqrt(10.0)) < 1e-12);
    QCOMPARE(t.last, 18.0);

    // 数值稳定性：均值远大于标准差时，朴素"平方和-均值平方"会丢失有效位，
    // Welford 仍应给出与上面完全一致的离散度
    StatEngine e2;
    feedTemps(e2, {1000010.0, 1000012.0, 1000014.0, 1000016.0, 1000018.0});
    const ChannelStat t2 = e2.snapshot().temp;
    QVERIFY(qAbs(t2.mean - 1000014.0) < 1e-6);
    QVERIFY(qAbs(t2.stddev - std::sqrt(10.0)) < 1e-6);

    // 单样本无离散度
    StatEngine e3;
    feedTemps(e3, {25.0});
    QCOMPARE(e3.snapshot().temp.stddev, 0.0);
}

void TestStatEngine::minMaxWithTimestamp()
{
    StatEngine e;
    const QDateTime base(QDate(2026, 9, 17), QTime(10, 0, 0, 0));

    e.slotSample(makeTimedSensor(25.0, 40.0, base, 1));
    e.slotSample(makeTimedSensor(30.0, 70.0, base.addSecs(1), 2));   // 温度最大
    e.slotSample(makeTimedSensor(20.0, 55.0, base.addSecs(2), 3));   // 温度最小
    e.slotSample(makeTimedSensor(27.0, 35.0, base.addSecs(3), 4));   // 湿度最小

    const StatSnapshot s = e.snapshot();
    QCOMPARE(s.temp.min, 20.0);
    QCOMPARE(s.temp.minTime, base.addSecs(2));
    QCOMPARE(s.temp.max, 30.0);
    QCOMPARE(s.temp.maxTime, base.addSecs(1));
    QCOMPARE(s.hum.min, 35.0);
    QCOMPARE(s.hum.max, 70.0);
    QCOMPARE(s.samples, qint64(4));
}

void TestStatEngine::histogramCoversAllSamples()
{
    StatEngine e;
    QVector<double> temps;
    for (int i = 0; i < 100; ++i)
        temps.append(double(i));   // 0..99 均匀分布
    feedTemps(e, temps);

    QVector<double> centers;
    QVector<int> counts;
    const double width = e.histogram(StatChannel::Temperature, 20, centers, counts);

    QCOMPARE(centers.size(), 20);
    QCOMPARE(counts.size(), 20);
    QVERIFY(width > 0.0);
    QVERIFY(qAbs(width - 99.0 / 20.0) < 1e-9);   // (max-min)/bins

    int sum = 0;
    for (int c : counts) {
        QVERIFY(c >= 0);
        sum += c;
    }
    QCOMPARE(sum, 100);   // 所有样本必须落箱，一个不漏（含最大值归入末箱）

    // 箱中心严格升序，供柱状图直接使用（setData 要求有序）
    for (int i = 1; i < centers.size(); ++i)
        QVERIFY(centers.at(i) > centers.at(i - 1));

    // 湿度通道在本用例中被喂入恒定值 50.0，同样应完整落箱
    QVector<double> hc;
    QVector<int> hn;
    QVERIFY(e.histogram(StatChannel::Humidity, 20, hc, hn) > 0.0);
    int hsum = 0;
    for (int c : hn)
        hsum += c;
    QCOMPARE(hsum, 100);

    // 全新引擎（无任何样本）：返回 0 且不产生数据
    // 注意 0.0 不能用 QCOMPARE 比对——Qt 的 fuzzy compare 对 0 恒不成立
    StatEngine empty;
    QVector<double> c2;
    QVector<int> n2;
    QVERIFY(empty.histogram(StatChannel::Temperature, 20, c2, n2) == 0.0);
    QVERIFY(c2.isEmpty());
    QVERIFY(n2.isEmpty());
}

void TestStatEngine::constantValueHistogram()
{
    StatEngine e;
    feedTemps(e, QVector<double>(10, 25.0));   // 恒温：min == max

    QVector<double> centers;
    QVector<int> counts;
    const double width = e.histogram(StatChannel::Temperature, 20, centers, counts);

    QVERIFY(width > 0.0);                       // 区间被撑开，未除零
    QVERIFY(qAbs(width - 1.0 / 20.0) < 1e-12);  // [24.5, 25.5] 等分 20 箱
    QCOMPARE(counts.size(), 20);

    int sum = 0;
    for (int c : counts)
        sum += c;
    QCOMPARE(sum, 10);
}

void TestStatEngine::lossRateFromProtoStats()
{
    StatEngine e;

    // 无任何帧时丢包率为 0（不能除零）
    QCOMPARE(e.snapshot().lossRate, 0.0);

    ProtoStats p;
    p.totalBytes = 1300;
    p.totalFrames = 100;
    p.validFrames = 95;
    p.crcErrors = 2;
    p.syncLosses = 3;
    e.slotProtoStats(p);

    const StatSnapshot s = e.snapshot();
    QVERIFY(qAbs(s.lossRate - 0.05) < 1e-12);   // (2+3)/100
    QCOMPARE(s.totalFrames, qint64(100));
    QCOMPARE(s.validFrames, qint64(95));
    QCOMPARE(s.crcErrors, qint64(2));
    QCOMPARE(s.syncLosses, qint64(3));
}

void TestStatEngine::rateWindowAndReset()
{
    StatEngine e;
    e.slotBytesChanged(1000, 200);
    feedTemps(e, {25.0, 25.1, 25.2, 25.3, 25.4});

    // 引擎内部 1s 定时器结算一次窗口，等待首个快照
    QSignalSpy spy(&e, &StatEngine::sigSnapshot);
    QVERIFY(spy.wait(2500));

    const StatSnapshot s = spy.at(0).at(0).value<StatSnapshot>();
    QCOMPARE(s.rxBps, 1000.0);      // 差值法：本秒新增字节即速率
    QCOMPARE(s.txBps, 200.0);
    QCOMPARE(s.frameRate, 5.0);     // 本秒 5 帧
    QCOMPARE(s.samples, qint64(5));
    QVERIFY(s.durationSecs >= 1);

    // 一键清零：统计量与速率全部归零
    e.slotReset();
    const StatSnapshot r = e.snapshot();
    QCOMPARE(r.samples, qint64(0));
    QCOMPARE(r.temp.count, qint64(0));
    QCOMPARE(r.temp.mean, 0.0);
    QCOMPARE(r.temp.stddev, 0.0);
    QCOMPARE(r.rxBps, 0.0);
    QCOMPARE(r.frameRate, 0.0);
    QCOMPARE(r.durationSecs, qint64(0));
    QCOMPARE(r.totalFrames, qint64(0));
}

// ---------------------------------------------------------------------------
// 测试辅助：内存数据库（每个用例独立，随对象析构自动释放）
// ---------------------------------------------------------------------------

static bool openMemoryDb(Database &db)
{
    return db.open(QStringLiteral(":memory:"));
}

// ---------------------------------------------------------------------------
// TestPasswordHasher：口令哈希（S8）
// ---------------------------------------------------------------------------

class TestPasswordHasher : public QObject
{
    Q_OBJECT

private slots:
    void saltIsRandomAndSized();
    void hashIsDeterministicAndSaltSensitive();
    void verifyAcceptsOnlyCorrectPassword();
    void iterationsChangeDigest();
};

void TestPasswordHasher::saltIsRandomAndSized()
{
    const QByteArray a = PasswordHasher::randomSalt();
    const QByteArray b = PasswordHasher::randomSalt();
    QCOMPARE(a.size(), 16);
    QCOMPARE(b.size(), 16);
    QVERIFY(a != b);   // 两次取盐必须不同，否则相同口令会得到相同摘要
    QCOMPARE(PasswordHasher::randomSalt(32).size(), 32);
}

void TestPasswordHasher::hashIsDeterministicAndSaltSensitive()
{
    const QByteArray salt = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QByteArray h1 = PasswordHasher::hash(QStringLiteral("secret"), salt, 100);
    const QByteArray h2 = PasswordHasher::hash(QStringLiteral("secret"), salt, 100);

    QCOMPARE(h1, h2);           // 同盐同口令 → 同摘要，登录校验才可能成功
    QCOMPARE(h1.size(), 32);    // SHA-256 输出 32 字节

    // 换盐即换摘要：抵御彩虹表与"一次破解多账号"的批量攻击
    QVERIFY(PasswordHasher::hash(QStringLiteral("secret"), PasswordHasher::randomSalt(), 100) != h1);
    // 换口令即换摘要
    QVERIFY(PasswordHasher::hash(QStringLiteral("secrt"), salt, 100) != h1);
}

void TestPasswordHasher::verifyAcceptsOnlyCorrectPassword()
{
    const QByteArray salt = PasswordHasher::randomSalt();
    const QByteArray digest = PasswordHasher::hash(QStringLiteral("Passw0rd!"), salt, 500);

    QVERIFY(PasswordHasher::verify(QStringLiteral("Passw0rd!"), salt, 500, digest));
    QVERIFY(!PasswordHasher::verify(QStringLiteral("passw0rd!"), salt, 500, digest));  // 大小写敏感
    QVERIFY(!PasswordHasher::verify(QStringLiteral("Passw0rd"), salt, 500, digest));
    QVERIFY(!PasswordHasher::verify(QStringLiteral("Passw0rd!"), salt, 499, digest));  // 轮数不符
    QVERIFY(!PasswordHasher::verify(QStringLiteral("Passw0rd!"), PasswordHasher::randomSalt(), 500, digest));
    QVERIFY(!PasswordHasher::verify(QStringLiteral("Passw0rd!"), salt, 500, QByteArray())); // 长度不符
    QVERIFY(!PasswordHasher::verify(QString(), salt, 500, digest));
}

void TestPasswordHasher::iterationsChangeDigest()
{
    const QByteArray salt = PasswordHasher::randomSalt();
    QVERIFY(PasswordHasher::hash(QStringLiteral("abc123"), salt, 1)
            != PasswordHasher::hash(QStringLiteral("abc123"), salt, 2));
}

// ---------------------------------------------------------------------------
// TestUserService：账号与鉴权（S8）
// ---------------------------------------------------------------------------

class TestUserService : public QObject
{
    Q_OBJECT

private slots:
    void defaultAdminCreated();
    void authenticateSuccessAndFailure();
    void disabledUserRejected();
    void createUserValidation();
    void changePasswordTakesEffect();
    void lastAdminProtection();
    void roleAndListingOrder();
};

void TestUserService::defaultAdminCreated()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);

    QCOMPARE(svc.userCount(), 0);
    QVERIFY(svc.ensureDefaultAdmin());       // 空库 → 植入默认管理员
    QCOMPARE(svc.userCount(), 1);
    QVERIFY(!svc.ensureDefaultAdmin());      // 已有用户 → 不再重复创建
    QVERIFY(svc.authenticate(QStringLiteral("admin"), QStringLiteral("admin123")).isValid());
    QVERIFY(svc.enabledAdminCount() == 1);
}

void TestUserService::authenticateSuccessAndFailure()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);
    QVERIFY(svc.ensureDefaultAdmin());

    QString err;
    const UserInfo u = svc.authenticate(QStringLiteral("admin"), QStringLiteral("admin123"), &err);
    QVERIFY(u.isValid());
    QVERIFY(u.role == UserRole::Admin);
    QVERIFY(u.enabled);
    QVERIFY(u.lastLogin.isValid());   // 登录成功后 last_login 被刷新
    QVERIFY(err.isEmpty());

    // 用户名大小写不敏感
    QVERIFY(svc.authenticate(QStringLiteral("ADMIN"), QStringLiteral("admin123")).isValid());

    // 口令错误 与 用户不存在 必须给出同一句提示，防止用户名枚举
    QString e1, e2;
    QVERIFY(!svc.authenticate(QStringLiteral("admin"), QStringLiteral("wrongpwd"), &e1).isValid());
    QVERIFY(!svc.authenticate(QStringLiteral("nobody"), QStringLiteral("admin123"), &e2).isValid());
    QCOMPARE(e1, e2);
    QVERIFY(!e1.isEmpty());
}

void TestUserService::disabledUserRejected()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);
    QVERIFY(svc.ensureDefaultAdmin());

    QString err;
    QVERIFY(svc.createUser(QStringLiteral("oper1"), QStringLiteral("oper123456"),
                           UserRole::Operator, QStringLiteral("操作员一号"), &err));
    const UserInfo op = svc.findByName(QStringLiteral("OPER1"));   // 查找同样忽略大小写
    QVERIFY(op.isValid());
    QCOMPARE(op.displayName, QStringLiteral("操作员一号"));

    QVERIFY(svc.setEnabled(op.id, false, &err));
    QString e;
    QVERIFY(!svc.authenticate(QStringLiteral("oper1"), QStringLiteral("oper123456"), &e).isValid());
    QVERIFY(e.contains(QStringLiteral("禁用")));

    QVERIFY(svc.setEnabled(op.id, true, &err));   // 重新启用后可正常登录
    QVERIFY(svc.authenticate(QStringLiteral("oper1"), QStringLiteral("oper123456")).isValid());
}

void TestUserService::createUserValidation()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);

    // 用户名规则：3~20 位、字母/下划线开头、不含空格
    QVERIFY(!svc.createUser(QStringLiteral("ab"), QStringLiteral("123456"), UserRole::Guest));
    QVERIFY(!svc.createUser(QStringLiteral("1abc"), QStringLiteral("123456"), UserRole::Guest));
    QVERIFY(!svc.createUser(QStringLiteral("has space"), QStringLiteral("123456"), UserRole::Guest));
    QVERIFY(!svc.createUser(QStringLiteral("中文名字"), QStringLiteral("123456"), UserRole::Guest));
    // 口令规则：不少于 6 位
    QVERIFY(!svc.createUser(QStringLiteral("valid_user"), QStringLiteral("123"), UserRole::Guest));
    QCOMPARE(svc.userCount(), 0);

    QVERIFY(svc.createUser(QStringLiteral("valid_user"), QStringLiteral("123456"), UserRole::Guest));
    QCOMPARE(svc.userCount(), 1);

    // 大小写不同但同名 → 视为重复
    QString err;
    QVERIFY(!svc.createUser(QStringLiteral("VALID_USER"), QStringLiteral("123456"), UserRole::Guest,
                            QString(), &err));
    QVERIFY(err.contains(QStringLiteral("已存在")));
}

void TestUserService::changePasswordTakesEffect()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);
    QVERIFY(svc.ensureDefaultAdmin());

    const UserInfo admin = svc.findByName(QStringLiteral("admin"));
    QString err;
    QVERIFY(svc.changePassword(admin.id, QStringLiteral("newpass123"), &err));
    QVERIFY(!svc.authenticate(QStringLiteral("admin"), QStringLiteral("admin123")).isValid());
    QVERIFY(svc.authenticate(QStringLiteral("admin"), QStringLiteral("newpass123")).isValid());

    // 弱口令被拒绝，且原口令仍然有效（改密失败不影响现有账号）
    QVERIFY(!svc.changePassword(admin.id, QStringLiteral("123"), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(svc.authenticate(QStringLiteral("admin"), QStringLiteral("newpass123")).isValid());

    // 不存在的用户
    QVERIFY(!svc.changePassword(99999, QStringLiteral("whatever1"), &err));
    QVERIFY(!err.isEmpty());
}

void TestUserService::lastAdminProtection()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);
    QVERIFY(svc.ensureDefaultAdmin());

    const UserInfo admin = svc.findByName(QStringLiteral("admin"));
    QString err;

    // 唯一管理员：不可删、不可禁用、不可降级，否则系统失去管理入口
    QVERIFY(!svc.removeUser(admin.id, 999, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!svc.setEnabled(admin.id, false, &err));
    QVERIFY(!svc.setRole(admin.id, UserRole::Guest, &err));
    QCOMPARE(svc.userCount(), 1);
    QCOMPARE(svc.enabledAdminCount(), 1);

    // 新增第二个管理员后，第一个才可以被降级/删除
    QVERIFY(svc.createUser(QStringLiteral("admin2"), QStringLiteral("admin12345"), UserRole::Admin));
    const UserInfo a2 = svc.findByName(QStringLiteral("admin2"));
    QCOMPARE(svc.enabledAdminCount(), 2);

    QVERIFY(svc.setRole(admin.id, UserRole::Operator, &err));
    QCOMPARE(svc.enabledAdminCount(), 1);
    QVERIFY(!svc.setRole(a2.id, UserRole::Guest, &err));   // 现在它成了唯一管理员

    // 不能删除自己（currentUserId 与被删 id 相同）
    QVERIFY(!svc.removeUser(a2.id, a2.id, &err));
    QVERIFY(svc.removeUser(admin.id, a2.id, &err));
    QCOMPARE(svc.userCount(), 1);
    QVERIFY(!svc.findById(admin.id).isValid());
}

void TestUserService::roleAndListingOrder()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    UserService svc(&db);
    QVERIFY(svc.ensureDefaultAdmin());
    QVERIFY(svc.createUser(QStringLiteral("guest1"), QStringLiteral("guest123"), UserRole::Guest,
                           QStringLiteral("访客甲")));
    QVERIFY(svc.createUser(QStringLiteral("oper1"), QStringLiteral("oper12345"), UserRole::Operator,
                           QStringLiteral("操作乙")));

    const QList<UserInfo> all = svc.listUsers();
    QCOMPARE(all.size(), 3);
    QVERIFY(all.at(0).role == UserRole::Admin);   // ORDER BY role ASC → 管理员在最前
    QCOMPARE(svc.enabledAdminCount(), 1);
    QVERIFY(svc.findById(all.at(1).id).isValid());
    QVERIFY(!svc.findById(99999).isValid());
    QVERIFY(!svc.findByName(QStringLiteral("ghost")).isValid());
}

// ---------------------------------------------------------------------------
// TestAuditService：操作审计（S8）
// ---------------------------------------------------------------------------

class TestAuditService : public QObject
{
    Q_OBJECT

private slots:
    void logCarriesCurrentUser();
    void queryFilterAndLimit();
    void clearRemovesAll();
};

void TestAuditService::logCarriesCurrentUser()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AuditService svc(&db);

    QSignalSpy spy(&svc, &AuditService::sigEntryAdded);

    svc.slotCurrentUser(-1, QString());
    QVERIFY(svc.log(QStringLiteral("LOGIN_FAILED"), QStringLiteral("用户=nobody"), false));
    svc.slotCurrentUser(7, QStringLiteral("admin"));
    QVERIFY(svc.log(QStringLiteral("FAN_COMMAND"), QStringLiteral("开")));
    QCOMPARE(spy.count(), 2);

    const QList<AuditEntry> all = svc.query(10);
    QCOMPARE(all.size(), 2);
    // 倒序返回：最新一条在前
    QCOMPARE(all.at(0).action, QStringLiteral("FAN_COMMAND"));
    QCOMPARE(all.at(0).username, QStringLiteral("admin"));
    QCOMPARE(all.at(0).userId, 7);
    QVERIFY(all.at(0).success);
    QVERIFY(all.at(0).ts.isValid());
    QCOMPARE(all.at(1).action, QStringLiteral("LOGIN_FAILED"));
    QVERIFY(!all.at(1).success);
    QCOMPARE(svc.count(), 2);
}

void TestAuditService::queryFilterAndLimit()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AuditService svc(&db);
    svc.slotCurrentUser(1, QStringLiteral("admin"));

    for (int i = 0; i < 5; ++i)
        svc.log(QStringLiteral("A"), QString::number(i));
    svc.log(QStringLiteral("B"), QStringLiteral("x"));

    QCOMPARE(svc.query(3).size(), 3);          // limit 生效
    QCOMPARE(svc.query(100, QStringLiteral("A")).size(), 5);
    QCOMPARE(svc.query(100, QStringLiteral("B")).size(), 1);
    QVERIFY(svc.query(100, QStringLiteral("ZZZ")).isEmpty());
    QCOMPARE(svc.count(), 6);
}

void TestAuditService::clearRemovesAll()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AuditService svc(&db);
    svc.log(QStringLiteral("A"));
    svc.log(QStringLiteral("B"));
    QCOMPARE(svc.count(), 2);
    QVERIFY(svc.clear());
    QCOMPARE(svc.count(), 0);
}

// ---------------------------------------------------------------------------
// TestAlarmStore：报警事件持久化（S8）
// ---------------------------------------------------------------------------

class TestAlarmStore : public QObject
{
    Q_OBJECT

private slots:
    void insertAssignsIdAndRoundTrips();
    void ackWorkflow();
    void clearAndCounts();
};

// 造一条报警事件
static AlarmEvent makeAlarm(AlarmChannel ch, AlarmLevel lv, AlarmKind kind,
                            double value, double threshold, const QDateTime &ts)
{
    AlarmEvent e;
    e.ts = ts;
    e.channel = ch;
    e.level = lv;
    e.kind = kind;
    e.value = value;
    e.threshold = threshold;
    e.message = QStringLiteral("%1%2: %3").arg(AlarmText::channel(ch), AlarmText::kind(kind),
                                               QString::number(value, 'f', 2));
    return e;
}

void TestAlarmStore::insertAssignsIdAndRoundTrips()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AlarmStore store(&db);

    const QDateTime ts(QDate(2026, 9, 17), QTime(10, 30, 15, 250));
    const AlarmEvent e = makeAlarm(AlarmChannel::Temperature, AlarmLevel::Critical,
                                   AlarmKind::HighTrigger, 41.5, 35.0, ts);
    const qint64 id = store.insert(e);
    QVERIFY(id > 0);   // 自增主键回填

    const QList<AlarmEvent> got = store.query(10);
    QCOMPARE(got.size(), 1);
    QCOMPARE(got.at(0).id, id);
    QCOMPARE(got.at(0).ts, ts);   // 毫秒精度往返无损
    QVERIFY(got.at(0).channel == AlarmChannel::Temperature);
    QVERIFY(got.at(0).level == AlarmLevel::Critical);
    QVERIFY(got.at(0).kind == AlarmKind::HighTrigger);
    QVERIFY(qAbs(got.at(0).value - 41.5) < 1e-9);
    QVERIFY(qAbs(got.at(0).threshold - 35.0) < 1e-9);
    QCOMPARE(got.at(0).message, e.message);
    QVERIFY(!got.at(0).acked);
    QCOMPARE(store.unackedCount(), 1);
    QCOMPARE(store.count(), 1);
}

void TestAlarmStore::ackWorkflow()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AlarmStore store(&db);

    const QDateTime ts = QDateTime::currentDateTime();
    const qint64 id1 = store.insert(makeAlarm(AlarmChannel::Temperature, AlarmLevel::Warning,
                                              AlarmKind::HighTrigger, 37.0, 35.0, ts));
    const qint64 id2 = store.insert(makeAlarm(AlarmChannel::Humidity, AlarmLevel::Warning,
                                              AlarmKind::LowTrigger, 15.0, 20.0, ts.addSecs(1)));
    QVERIFY(id1 > 0 && id2 > 0);
    QCOMPARE(store.unackedCount(), 2);

    QVERIFY(store.setAcked(id1));
    QCOMPARE(store.unackedCount(), 1);

    const QList<AlarmEvent> un = store.query(10, true);   // 只取未确认
    QCOMPARE(un.size(), 1);
    QCOMPARE(un.at(0).id, id2);

    QVERIFY(store.ackAll());
    QCOMPARE(store.unackedCount(), 0);
    QVERIFY(store.query(10, true).isEmpty());
    QCOMPARE(store.query(10).size(), 2);   // 确认只是改状态，不删记录

    // 倒序：后插入的在前
    QCOMPARE(store.query(10).at(0).id, id2);
}

void TestAlarmStore::clearAndCounts()
{
    Database db;
    QVERIFY(openMemoryDb(db));
    AlarmStore store(&db);

    const QDateTime ts = QDateTime::currentDateTime();
    store.insert(makeAlarm(AlarmChannel::Temperature, AlarmLevel::Info, AlarmKind::Recovered, 25.0, 35.0, ts));
    store.insert(makeAlarm(AlarmChannel::Temperature, AlarmLevel::Info, AlarmKind::Recovered, 25.0, 35.0, ts));
    QCOMPARE(store.count(), 2);
    QVERIFY(store.clear());
    QCOMPARE(store.count(), 0);
    QVERIFY(store.query(10).isEmpty());
}

// ---------------------------------------------------------------------------
// TestAlarmEngine：阈值报警与去抖（S8）
// ---------------------------------------------------------------------------

class TestAlarmEngine : public QObject
{
    Q_OBJECT

private slots:
    void debounceSuppressesJitter();   // 连续N帧越限才触发，抖动被抑制
    void recoverNeedsDebounceToo();    // 恢复同样需要连续N帧正常
    void levelGradingByDeviation();    // 按越限幅度分级
    void lowThresholdTrigger();        // 低于下限
    void humidityChannel();            // 湿度通道
    void disabledEmitsNothing();       // 总开关/通道开关关闭时静默
    void invalidThresholdsRejected();  // 非法阈值被拒绝且保持原值
    void clearActiveResetsState();     // 手动清除活动报警
};

// 默认阈值：温度 [5, 30]，去抖 3 帧，湿度关闭
static AlarmThresholds defaultThresholds()
{
    AlarmThresholds t;
    t.enabled = true;
    t.tempEnabled = true;
    t.tempLow = 5.0;
    t.tempHigh = 30.0;
    t.humEnabled = false;
    t.debounceFrames = 3;
    return t;
}

void TestAlarmEngine::debounceSuppressesJitter()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));

    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);
    QSignalSpy activeSpy(&eng, &AlarmEngine::sigActiveChanged);
    const QDateTime ts = QDateTime::currentDateTime();

    eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, 1));   // 越限第 1 帧
    eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, 2));   // 第 2 帧
    QCOMPARE(spy.count(), 0);                                 // 未达去抖帧数：不报警
    QVERIFY(!eng.hasActiveAlarm());

    eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, 3));   // 第 3 帧 → 触发
    QCOMPARE(spy.count(), 1);
    QVERIFY(eng.hasActiveAlarm());
    QCOMPARE(activeSpy.count(), 1);
    QVERIFY(activeSpy.at(0).at(0).toBool());

    const AlarmEvent e = spy.at(0).at(0).value<AlarmEvent>();
    QVERIFY(e.kind == AlarmKind::HighTrigger);
    QVERIFY(e.channel == AlarmChannel::Temperature);
    QVERIFY(qAbs(e.threshold - 30.0) < 1e-9);
    QVERIFY(qAbs(e.value - 31.0) < 1e-9);
    QVERIFY(e.level == AlarmLevel::Info);   // 偏差 1.0°C ≤ 2 → 提示级
    QVERIFY(!e.message.isEmpty());

    // 持续越限不应重复触发（否则每 200ms 就产生一条新报警）
    eng.slotSensorData(makeTimedSensor(32.0, 50.0, ts, 4));
    eng.slotSensorData(makeTimedSensor(33.0, 50.0, ts, 5));
    QCOMPARE(spy.count(), 1);

    // 偶发单帧抖动（回到正常再越限）不应打断已触发的报警状态
    eng.slotSensorData(makeTimedSensor(25.0, 50.0, ts, 6));   // 正常 1 帧
    eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, 7));   // 又越限
    QCOMPARE(spy.count(), 1);
    QVERIFY(eng.hasActiveAlarm());
}

void TestAlarmEngine::recoverNeedsDebounceToo()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));
    const QDateTime ts = QDateTime::currentDateTime();

    for (int i = 0; i < 3; ++i)
        eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, i + 1));   // 先触发

    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);
    QCOMPARE(spy.count(), 0);
    QVERIFY(eng.hasActiveAlarm());

    eng.slotSensorData(makeTimedSensor(25.0, 50.0, ts, 10));   // 正常 1
    eng.slotSensorData(makeTimedSensor(25.0, 50.0, ts, 11));   // 正常 2
    QCOMPARE(spy.count(), 0);
    QVERIFY(eng.hasActiveAlarm());

    eng.slotSensorData(makeTimedSensor(25.0, 50.0, ts, 12));   // 正常 3 → 恢复
    QCOMPARE(spy.count(), 1);
    QVERIFY(!eng.hasActiveAlarm());

    const AlarmEvent e = spy.at(0).at(0).value<AlarmEvent>();
    QVERIFY(e.kind == AlarmKind::Recovered);
    QVERIFY(e.level == AlarmLevel::Info);
    QVERIFY(e.message.contains(QStringLiteral("恢复")));

    // 恢复后再次越限，应能重新触发（状态机已复位）
    for (int i = 0; i < 3; ++i)
        eng.slotSensorData(makeTimedSensor(40.0, 50.0, ts, 20 + i));
    QCOMPARE(spy.count(), 2);
    QVERIFY(eng.hasActiveAlarm());
    QVERIFY(!eng.activeSummary().isEmpty());
}

void TestAlarmEngine::levelGradingByDeviation()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));
    const QDateTime ts = QDateTime::currentDateTime();
    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);

    // 偏差 1.0 → 提示；触发后需先恢复才能再触发，故每轮独立建引擎更直观
    auto levelFor = [&](double value) {
        AlarmEngine e2;
        e2.setThresholds(defaultThresholds());
        QSignalSpy s(&e2, &AlarmEngine::sigAlarm);
        for (int i = 0; i < 3; ++i)
            e2.slotSensorData(makeTimedSensor(value, 50.0, ts, i + 1));
        return (s.count() == 1) ? s.at(0).at(0).value<AlarmEvent>().level : AlarmLevel::Info;
    };

    QVERIFY(levelFor(31.0) == AlarmLevel::Info);      // 偏差 1.0 ≤ 2
    QVERIFY(levelFor(33.0) == AlarmLevel::Warning);   // 偏差 3.0 ∈ (2, 5]
    QVERIFY(levelFor(36.0) == AlarmLevel::Critical);  // 偏差 6.0 > 5
    QCOMPARE(spy.count(), 0);   // 本用例的 eng 从未收到数据
}

void TestAlarmEngine::lowThresholdTrigger()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));
    const QDateTime ts = QDateTime::currentDateTime();
    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);

    for (int i = 0; i < 3; ++i)
        eng.slotSensorData(makeTimedSensor(3.0, 50.0, ts, i + 1));   // 低于下限 5.0

    QCOMPARE(spy.count(), 1);
    const AlarmEvent e = spy.at(0).at(0).value<AlarmEvent>();
    QVERIFY(e.kind == AlarmKind::LowTrigger);
    QVERIFY(qAbs(e.threshold - 5.0) < 1e-9);
    QVERIFY(e.level == AlarmLevel::Info);   // 偏差 2.0，未超过警告判据
}

void TestAlarmEngine::humidityChannel()
{
    AlarmEngine eng;
    AlarmThresholds t = defaultThresholds();
    t.humEnabled = true;
    t.humLow = 20.0;
    t.humHigh = 90.0;
    QVERIFY(eng.setThresholds(t));

    const QDateTime ts = QDateTime::currentDateTime();
    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);

    for (int i = 0; i < 3; ++i)
        eng.slotSensorData(makeTimedSensor(25.0, 95.0, ts, i + 1));   // 温度正常，湿度越限

    QCOMPARE(spy.count(), 1);
    const AlarmEvent e = spy.at(0).at(0).value<AlarmEvent>();
    QVERIFY(e.channel == AlarmChannel::Humidity);
    QVERIFY(e.level == AlarmLevel::Warning);   // 湿度越限统一按警告处理
    QVERIFY(e.message.contains(QStringLiteral("湿度")));
}

void TestAlarmEngine::disabledEmitsNothing()
{
    const QDateTime ts = QDateTime::currentDateTime();

    // 总开关关闭
    AlarmEngine eng;
    AlarmThresholds off = defaultThresholds();
    off.enabled = false;
    QVERIFY(eng.setThresholds(off));
    QSignalSpy spy(&eng, &AlarmEngine::sigAlarm);
    for (int i = 0; i < 5; ++i)
        eng.slotSensorData(makeTimedSensor(80.0, 50.0, ts, i + 1));
    QCOMPARE(spy.count(), 0);
    QVERIFY(!eng.hasActiveAlarm());

    // 只关温度通道：湿度未使能时同样静默
    AlarmEngine eng2;
    AlarmThresholds t2 = defaultThresholds();
    t2.tempEnabled = false;
    QVERIFY(eng2.setThresholds(t2));
    QSignalSpy spy2(&eng2, &AlarmEngine::sigAlarm);
    for (int i = 0; i < 5; ++i)
        eng2.slotSensorData(makeTimedSensor(80.0, 99.0, ts, i + 1));
    QCOMPARE(spy2.count(), 0);
}

void TestAlarmEngine::invalidThresholdsRejected()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));
    QSignalSpy spy(&eng, &AlarmEngine::sigThresholdsChanged);

    AlarmThresholds bad = defaultThresholds();
    bad.tempLow = 40.0;
    bad.tempHigh = 30.0;      // 下限 ≥ 上限
    QVERIFY(!eng.setThresholds(bad));
    QCOMPARE(spy.count(), 0);
    QVERIFY(qAbs(eng.thresholds().tempHigh - 30.0) < 1e-9);
    QVERIFY(qAbs(eng.thresholds().tempLow - 5.0) < 1e-9);

    AlarmThresholds bad2 = defaultThresholds();
    bad2.debounceFrames = 0;  // 去抖帧数非法
    QVERIFY(!eng.setThresholds(bad2));
    QCOMPARE(spy.count(), 0);

    // 合法配置：接受并发出信号
    AlarmThresholds ok = defaultThresholds();
    ok.tempHigh = 45.0;
    QVERIFY(eng.setThresholds(ok));
    QCOMPARE(spy.count(), 1);
    QVERIFY(qAbs(eng.thresholds().tempHigh - 45.0) < 1e-9);
}

void TestAlarmEngine::clearActiveResetsState()
{
    AlarmEngine eng;
    QVERIFY(eng.setThresholds(defaultThresholds()));
    const QDateTime ts = QDateTime::currentDateTime();

    for (int i = 0; i < 3; ++i)
        eng.slotSensorData(makeTimedSensor(31.0, 50.0, ts, i + 1));
    QVERIFY(eng.hasActiveAlarm());

    QSignalSpy activeSpy(&eng, &AlarmEngine::sigActiveChanged);
    eng.slotClearActive();
    QVERIFY(!eng.hasActiveAlarm());
    QVERIFY(eng.activeSummary().isEmpty());
    QCOMPARE(activeSpy.count(), 1);
    QVERIFY(!activeSpy.at(0).at(0).toBool());
}

// ---------------------------------------------------------------------------
// TestSession：会话与权限（S8）
// ---------------------------------------------------------------------------

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void permissionMatrix();
    void loginGrantsPermissionsByRole();
    void logoutRevokesAll();
    void updateUserRoleEmitsPermissionChange();
};

void TestSession::permissionMatrix()
{
    // 访客：只读
    QVERIFY(!Permission::canControl(UserRole::Guest));
    QVERIFY(!Permission::canConnectSerial(UserRole::Guest));
    QVERIFY(!Permission::canChangeSetpoint(UserRole::Guest));
    QVERIFY(!Permission::canRecord(UserRole::Guest));
    QVERIFY(!Permission::canConfigureAlarm(UserRole::Guest));
    QVERIFY(!Permission::canManageUsers(UserRole::Guest));
    QVERIFY(!Permission::canPurgeData(UserRole::Guest));

    // 操作员：可采集与控制，不可管用户/清库
    QVERIFY(Permission::canControl(UserRole::Operator));
    QVERIFY(Permission::canConnectSerial(UserRole::Operator));
    QVERIFY(Permission::canChangeSetpoint(UserRole::Operator));
    QVERIFY(Permission::canRecord(UserRole::Operator));
    QVERIFY(Permission::canConfigureAlarm(UserRole::Operator));
    QVERIFY(!Permission::canManageUsers(UserRole::Operator));
    QVERIFY(!Permission::canPurgeData(UserRole::Operator));

    // 管理员：全权
    QVERIFY(Permission::canControl(UserRole::Admin));
    QVERIFY(Permission::canManageUsers(UserRole::Admin));
    QVERIFY(Permission::canPurgeData(UserRole::Admin));

    // 角色文本与整数映射（越界值回落为最小权限的访客）
    QCOMPARE(UserText::role(UserRole::Admin), QStringLiteral("管理员"));
    QCOMPARE(UserText::role(UserRole::Operator), QStringLiteral("操作员"));
    QCOMPARE(UserText::role(UserRole::Guest), QStringLiteral("访客"));
    QVERIFY(UserText::roleFromInt(0) == UserRole::Admin);
    QVERIFY(UserText::roleFromInt(1) == UserRole::Operator);
    QVERIFY(UserText::roleFromInt(2) == UserRole::Guest);
    QVERIFY(UserText::roleFromInt(99) == UserRole::Guest);
}

void TestSession::loginGrantsPermissionsByRole()
{
    Session s;
    QVERIFY(!s.isLoggedIn());
    QVERIFY(!s.canControl());
    QCOMPARE(s.userId(), -1);

    QSignalSpy loginSpy(&s, &Session::sigLoggedIn);
    QSignalSpy userSpy(&s, &Session::sigUserChanged);
    QSignalSpy permSpy(&s, &Session::sigPermissionsChanged);

    UserInfo op;
    op.id = 2;
    op.username = QStringLiteral("oper1");
    op.displayName = QStringLiteral("操作员");
    op.role = UserRole::Operator;
    s.login(op);

    QCOMPARE(loginSpy.count(), 1);
    QCOMPARE(userSpy.count(), 1);
    QCOMPARE(userSpy.at(0).at(0).toInt(), 2);
    QCOMPARE(userSpy.at(0).at(1).toString(), QStringLiteral("oper1"));
    QCOMPARE(permSpy.count(), 1);
    QVERIFY(s.isLoggedIn());
    QVERIFY(s.canControl());
    QVERIFY(!s.canManageUsers());
    QCOMPARE(s.roleText(), QStringLiteral("操作员"));

    UserInfo guest;
    guest.id = 3;
    guest.username = QStringLiteral("guest1");
    guest.role = UserRole::Guest;
    s.login(guest);
    QVERIFY(!s.canControl());
    QVERIFY(!s.canManageUsers());

    UserInfo admin;
    admin.id = 1;
    admin.username = QStringLiteral("admin");
    admin.role = UserRole::Admin;
    s.login(admin);
    QVERIFY(s.canControl());
    QVERIFY(s.canManageUsers());
    QVERIFY(s.canPurgeData());
}

void TestSession::logoutRevokesAll()
{
    Session s;
    UserInfo a;
    a.id = 1;
    a.username = QStringLiteral("admin");
    a.role = UserRole::Admin;
    s.login(a);

    QSignalSpy outSpy(&s, &Session::sigLoggedOut);
    s.logout();
    QCOMPARE(outSpy.count(), 1);
    QVERIFY(!s.isLoggedIn());
    QVERIFY(!s.canControl());
    QVERIFY(!s.canManageUsers());
    QCOMPARE(s.userId(), -1);
    QVERIFY(s.userName().isEmpty());

    s.logout();   // 重复注销不应再次发信号
    QCOMPARE(outSpy.count(), 1);
}

void TestSession::updateUserRoleEmitsPermissionChange()
{
    Session s;
    UserInfo u;
    u.id = 5;
    u.username = QStringLiteral("bob");
    u.displayName = QStringLiteral("Bob");
    u.role = UserRole::Operator;
    s.login(u);

    QSignalSpy permSpy(&s, &Session::sigPermissionsChanged);

    // 改的是别人的资料：当前会话不受影响
    UserInfo other = u;
    other.id = 9;
    other.role = UserRole::Admin;
    s.updateUser(other);
    QCOMPARE(permSpy.count(), 0);
    QVERIFY(!s.canManageUsers());

    // 自己被提权：广播权限变化，界面据此解锁管理入口
    UserInfo promoted = u;
    promoted.role = UserRole::Admin;
    promoted.displayName = QStringLiteral("Bobby");
    s.updateUser(promoted);
    QCOMPARE(permSpy.count(), 1);
    QVERIFY(s.canManageUsers());
    QCOMPARE(s.user().displayName, QStringLiteral("Bobby"));
}

// ---------------------------------------------------------------------------
// 自定义 main：依次运行各测试类
// ---------------------------------------------------------------------------

/**
 * @brief 运行一个测试类
 * @param reportDir 非空时把该类的报告写入 <reportDir>/<类名>.txt。
 *        一个 exe 里串行跑多个 qExec，若都输出到同一目标会互相覆盖，
 *        因此按类名分文件，命令行/CI 可逐个收集；留空则保持控制台输出，
 *        在 Qt Creator 里直接运行时行为不变。
 */
static int runOne(QObject &test, const QString &className, int argc, char *argv[], const QString &reportDir)
{
    if (reportDir.isEmpty())
        return QTest::qExec(&test, argc, argv);

    QByteArray prog = QFile::encodeName(QString::fromLocal8Bit(argv[0]));
    QByteArray opt("-o");
    QByteArray target = QFile::encodeName(
        QDir(reportDir).filePath(className + QStringLiteral(".txt")) + QStringLiteral(",txt"));
    char *fakeArgv[] = {prog.data(), opt.data(), target.data()};
    return QTest::qExec(&test, 3, fakeArgv);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // QSignalSpy 取自定义类型参数值需要元类型已注册
    qRegisterMetaType<SensorData>();
    qRegisterMetaType<ProtoStats>();
    qRegisterMetaType<StatSnapshot>();
    qRegisterMetaType<AlarmEvent>();
    qRegisterMetaType<AlarmThresholds>();
    qRegisterMetaType<UserInfo>();
    qRegisterMetaType<AuditEntry>();

    const QString reportDir = qEnvironmentVariable("TEST_REPORT_DIR");
    if (!reportDir.isEmpty())
        QDir().mkpath(reportDir);

    int status = 0;

    TestCrc crcTest;
    status |= runOne(crcTest, QStringLiteral("TestCrc"), argc, argv, reportDir);

    TestProtocol protoTest;
    status |= runOne(protoTest, QStringLiteral("TestProtocol"), argc, argv, reportDir);

    TestControlRouter routerTest;
    status |= runOne(routerTest, QStringLiteral("TestControlRouter"), argc, argv, reportDir);

    TestTempController tempTest;
    status |= runOne(tempTest, QStringLiteral("TestTempController"), argc, argv, reportDir);

    TestCsvStorage csvTest;
    status |= runOne(csvTest, QStringLiteral("TestCsvStorage"), argc, argv, reportDir);

    TestLogReplayer replayTest;
    status |= runOne(replayTest, QStringLiteral("TestLogReplayer"), argc, argv, reportDir);

    TestStatEngine statTest;
    status |= runOne(statTest, QStringLiteral("TestStatEngine"), argc, argv, reportDir);

    TestPasswordHasher hashTest;
    status |= runOne(hashTest, QStringLiteral("TestPasswordHasher"), argc, argv, reportDir);

    TestUserService userTest;
    status |= runOne(userTest, QStringLiteral("TestUserService"), argc, argv, reportDir);

    TestAuditService auditTest;
    status |= runOne(auditTest, QStringLiteral("TestAuditService"), argc, argv, reportDir);

    TestAlarmStore alarmStoreTest;
    status |= runOne(alarmStoreTest, QStringLiteral("TestAlarmStore"), argc, argv, reportDir);

    TestAlarmEngine alarmTest;
    status |= runOne(alarmTest, QStringLiteral("TestAlarmEngine"), argc, argv, reportDir);

    TestSession sessionTest;
    status |= runOne(sessionTest, QStringLiteral("TestSession"), argc, argv, reportDir);

    return status;
}

#include "test_protocol.moc"
