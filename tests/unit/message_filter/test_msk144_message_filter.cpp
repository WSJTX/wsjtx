#include <QtTest>

#include "MessageFilterRules.hpp"

class TestMSK144MessageFilter : public QObject
{
    Q_OBJECT

private slots:
    void testBlacklist()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.filtersForWord2 = true;
        ctx.blacklisted = true;
        ctx.blacklistKeywords = QStringList() << "K1ABC";

        auto result = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ K1ABC FN20", ctx).result;
        QVERIFY(result.filtered);
        QVERIFY(result.shouldReturn);
    }

    void testWhitelist()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.filtersForWord2 = true;
        ctx.whitelisted = true;
        ctx.whitelistKeywords = QStringList() << "K1ABC";

        auto result1 = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ K1ABC FN20", ctx).result;
        QVERIFY(!result1.filtered);
        QVERIFY(!result1.shouldReturn);

        auto result2 = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ W1AW FN20", ctx).result;
        QVERIFY(result2.filtered);
        QVERIFY(result2.shouldReturn);
    }

    void testBypass()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.bypass = true;
        ctx.filtersForWord2 = true;
        ctx.blacklisted = true;
        ctx.blacklistKeywords = QStringList() << "K1ABC";

        auto result = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ K1ABC FN20", ctx).result;
        QVERIFY(!result.filtered);
        QVERIFY(!result.shouldReturn);
    }

    void testDirectionalCQ()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.filtersForWord2 = true;
        ctx.blacklisted = true;
        ctx.blacklistKeywords = QStringList() << "K1ABC";

        auto result = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ DX K1ABC FN20", ctx).result;
        QVERIFY(result.filtered);
        QVERIFY(result.shouldReturn);
    }

    void testAlwaysPass()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.filtersForWord2 = true;
        ctx.alwaysPass = true;
        ctx.blacklisted = true;
        ctx.passKeywords = QStringList() << "K1ABC";
        ctx.blacklistKeywords = QStringList() << "K1ABC";

        auto result = MessageFilterRules::evaluateMSK144Text("060522 -10  0.3  815 # CQ K1ABC FN20", ctx).result;
        QVERIFY(!result.filtered);
        QVERIFY(!result.shouldReturn);
    }
};

QTEST_MAIN(TestMSK144MessageFilter)
#include "test_msk144_message_filter.moc"
