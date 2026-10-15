#include "InputProfile.h"
#include "SwitchEngine.h"
#include <QtTest>

class ImeCoreTests : public QObject {
    Q_OBJECT
private slots:
    void normalizeProfileIds() {
        auto us = InputProfile::parse(QStringLiteral("0x0409:0x00000409"));
        QVERIFY(us.isUS()); QCOMPARE(us.id, QStringLiteral("0409:00000409"));
        auto chinese = InputProfile::parse(QStringLiteral("0804:{81d4e9c9-1d3b-41bc-9e6c-4b40bf79e35e}{fa550b04-5ad7-411f-a5ac-ca038ec515d7}"));
        QVERIFY(chinese.isChinese()); QVERIFY(!chinese.keyboard); QVERIFY(!chinese.id.isEmpty());
        QVERIFY(InputProfile::parse(QStringLiteral("0409:00010409")).isUS() == false);
        QVERIFY(InputProfile::parse(QStringLiteral("0409:invalid")).id.isEmpty());
        QVERIFY(InputProfile::parse(QStringLiteral("0409:00000409;0804:00000804")).id.isEmpty());
    }
    void longPressIsOneToggle() {
        KeyTracker keys;
        QCOMPARE(keys.process(VK_LCONTROL, true, true, 0), KeyTracker::Pass);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
        for (int repeat = 0; repeat < 8; ++repeat) QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, false, true, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
    }
    void extraModifiersDoNotStealShortcut() {
        KeyTracker keys;
        keys.prime(VK_LCONTROL, true); keys.prime(VK_LSHIFT, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
        keys.process(VK_SPACE, false, true, 0); keys.prime(VK_LSHIFT, false); keys.prime(VK_LWIN, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
    }
    void pausedAndOrdinarySpacePassThrough() {
        KeyTracker keys;
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Pass);
        keys.process(VK_SPACE, false, true, 0); keys.prime(VK_RCONTROL, true);
        QCOMPARE(keys.process(VK_SPACE, true, false, 0), KeyTracker::Pass);
    }
    void disablingDuringPressPreservesKeyPair() {
        KeyTracker keys; keys.prime(VK_RCONTROL, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 0), KeyTracker::Toggle);
        QCOMPARE(keys.process(VK_SPACE, false, false, 0), KeyTracker::Suppress);
        QCOMPARE(keys.process(VK_SPACE, true, false, 0), KeyTracker::Pass);
    }
    void alternativeShortcut() {
        KeyTracker keys; keys.prime(VK_LMENU, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 2), KeyTracker::Toggle);
        keys.process(VK_SPACE, false, true, 2); keys.prime(VK_LMENU, false); keys.prime(VK_LCONTROL, true); keys.prime(VK_LSHIFT, true);
        QCOMPARE(keys.process(VK_SPACE, true, true, 1), KeyTracker::Toggle);
    }
};
QTEST_GUILESS_MAIN(ImeCoreTests)
#include "tst_imecore.moc"
