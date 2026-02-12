#include <QtTest>
#include "../MessageFilter.hpp"

class TestMessageFilter : public QObject
{
    Q_OBJECT

private slots:
    void testContainsAny()
    {
        QString text = "The quick brown fox jumps over the lazy dog";
        QStringList keywords = QStringList() << "fox" << "cat" << "";
        QVERIFY(MessageFilter::containsAny(text, keywords));

        keywords = QStringList() << "cat" << "bird";
        QVERIFY(!MessageFilter::containsAny(text, keywords));

        keywords = QStringList();
        QVERIFY(!MessageFilter::containsAny(text, keywords));
    }

    void testStartsWithAny()
    {
        QString text = "WSJT-X is great";
        QStringList keywords = QStringList() << "WSJT" << "GWS" << "";
        QVERIFY(MessageFilter::startsWithAny(text, keywords));

        keywords = QStringList() << "is" << "great";
        QVERIFY(!MessageFilter::startsWithAny(text, keywords));

        keywords = QStringList();
        QVERIFY(!MessageFilter::startsWithAny(text, keywords));
    }
};

QTEST_MAIN(TestMessageFilter)
#include "test_message_filter.moc"
