/**
 * @file    test_protocol.cpp
 * @brief   协议层单元测试（QTest）
 *
 * 覆盖内容：
 *  - CRC-8-ATM 已知向量校验（设计文档 3.2 节示例）
 *  - 完整帧 / 逐字节断帧 / 粘包多帧 / 噪声前缀 解析
 *  - 坏 CRC 丢弃计数、帧尾失配重同步、载荷内 0xAA 干扰免疫
 *
 * 运行方式：Qt Creator 打开 tests/test_protocol.pro 直接运行，
 * 或命令行 nmake 后执行 build 目录下的 test_protocol.exe。
 */

#include <QtTest>

#include "Crc8.h"
#include "ProtocolEngine.h"

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

// ---------------------------------------------------------------------------
// 自定义 main：依次运行两个测试类
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    int status = 0;
    TestCrc crcTest;
    status |= QTest::qExec(&crcTest, argc, argv);

    TestProtocol protoTest;
    status |= QTest::qExec(&protoTest, argc, argv);

    return status;
}

#include "test_protocol.moc"
