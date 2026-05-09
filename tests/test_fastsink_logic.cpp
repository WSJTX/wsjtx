#include <QtTest>
#include "../MessageFilterLogic.hpp"
#include "../Decoder/decodedtext.h"
#include "../MessageFilter.hpp"
#include "../Configuration.hpp"
#include "../commons.h"
#include "../widgets/itoneAndicw.h"

// Define missing symbols for linker
dec_data_t dec_data;
int volatile itone[MAX_NUM_SYMBOLS];
int volatile icw[NUM_CW_SYMBOLS];
float gran() { return 0.0f; }

// Dummy LogBook for testing (minimal implementation)
class TestFastSinkLogic : public QObject
{
    Q_OBJECT

private slots:
    void testMSK144Blacklist()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.specOp = Configuration::SpecialOperatingActivity::NONE;
        ctx.bypass = false;
        ctx.filtersForWord2 = true;
        ctx.filtersForWaitAndPounceOnly = false;
        ctx.alwaysPass = false;
        ctx.blacklisted = true;
        ctx.whitelisted = false;
        ctx.blacklistKeywords = QStringList() << "K1ABC";
        
        // Decoded message: "2023-01-01 12:00:00  -10  0.5 1500 # CQ K1ABC FN20"
        // DecodedText expects a specific format. Let's provide a realistic one.
        // 060522 -10  0.3  815 # CQ K1ABC FN20
        DecodedText dt("060522 -10  0.3  815 # CQ K1ABC FN20");
        
        auto result = MessageFilterLogic::evaluateMSK144(dt, ctx, nullptr);
        QVERIFY(result.filtered);
        QVERIFY(result.shouldReturn);
    }

    void testMSK144Whitelist()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.specOp = Configuration::SpecialOperatingActivity::NONE;
        ctx.bypass = false;
        ctx.filtersForWord2 = true;
        ctx.filtersForWaitAndPounceOnly = false;
        ctx.alwaysPass = false;
        ctx.blacklisted = false;
        ctx.whitelisted = true;
        ctx.whitelistKeywords = QStringList() << "K1ABC";
        
        // Match
        DecodedText dt1("060522 -10  0.3  815 # CQ K1ABC FN20");
        auto result1 = MessageFilterLogic::evaluateMSK144(dt1, ctx, nullptr);
        QVERIFY(!result1.filtered);
        QVERIFY(!result1.shouldReturn);

        // No match
        DecodedText dt2("060522 -10  0.3  815 # CQ W1AW FN20");
        auto result2 = MessageFilterLogic::evaluateMSK144(dt2, ctx, nullptr);
        QVERIFY(result2.filtered);
        QVERIFY(result2.shouldReturn);
    }

    void testMSK144Bypass()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.specOp = Configuration::SpecialOperatingActivity::NONE;
        ctx.bypass = true;
        ctx.filtersForWord2 = true;
        ctx.filtersForWaitAndPounceOnly = false;
        ctx.alwaysPass = false;
        ctx.blacklisted = true;
        ctx.blacklistKeywords = QStringList() << "K1ABC";
        
        DecodedText dt("060522 -10  0.3  815 # CQ K1ABC FN20");
        auto result = MessageFilterLogic::evaluateMSK144(dt, ctx, nullptr);
        QVERIFY(!result.filtered); // Filtered flag in result is false when bypass is on? 
        // Wait, in code: if (!ctx.bypass) result.filtered = true;
        // So result.filtered should be false.
        QVERIFY(!result.shouldReturn);
    }

    void testMSK144DirectionalCQ()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.specOp = Configuration::SpecialOperatingActivity::NONE;
        ctx.bypass = false;
        ctx.filtersForWord2 = true;
        ctx.filtersForWaitAndPounceOnly = false;
        ctx.alwaysPass = false;
        ctx.blacklisted = true;
        ctx.blacklistKeywords = QStringList() << "K1ABC";
        
        // Directional CQ: "CQ DX K1ABC FN20"
        // Word 1: CQ DX, Word 2: K1ABC
        DecodedText dt("060522 -10  0.3  815 # CQ DX K1ABC FN20");
        auto result = MessageFilterLogic::evaluateMSK144(dt, ctx, nullptr);
        QVERIFY(result.filtered);
        QVERIFY(result.shouldReturn);
    }

    void testMSK144AlwaysPass()
    {
        MessageFilterLogic::FilterContext ctx;
        ctx.specOp = Configuration::SpecialOperatingActivity::NONE;
        ctx.bypass = false;
        ctx.filtersForWord2 = true;
        ctx.alwaysPass = true;
        ctx.blacklisted = true;
        ctx.passKeywords = QStringList() << "K1ABC";
        ctx.blacklistKeywords = QStringList() << "K1ABC";
        
        DecodedText dt("060522 -10  0.3  815 # CQ K1ABC FN20");
        auto result = MessageFilterLogic::evaluateMSK144(dt, ctx, nullptr);
        QVERIFY(!result.filtered);
        QVERIFY(!result.shouldReturn);
    }
};

QTEST_MAIN(TestFastSinkLogic)
#include "test_fastsink_logic.moc"
