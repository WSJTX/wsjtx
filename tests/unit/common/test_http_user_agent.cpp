#include <QtTest>

#include "revision_utils.hpp"

class TestHttpUserAgent final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void labelsDisplayRevision ()
  {
    auto const display = display_revision ();
    auto const build_revision = revision ();

    if (build_revision.isEmpty ())
      {
        QVERIFY (display == "(revision unavailable)"
                 || display.contains (", build unavailable)"));
      }
    else if (display.startsWith ("(source "))
      {
        QVERIFY (display.endsWith (", build " + build_revision + ")"));
      }
    else
      {
        QCOMPARE (display, "(revision " + build_revision + ")");
      }
  }

  void includesApplicationIdentityAndPlatformContext ()
  {
    auto const user_agent = http_user_agent ();

    QVERIFY (user_agent.startsWith ("WSJT-X/"));
    QVERIFY (user_agent.contains (version ()));

    auto const build_revision = revision ();
    if (!build_revision.isEmpty ())
      {
        QVERIFY (user_agent.contains ("_" + build_revision));
      }

    QVERIFY (user_agent.contains ('('));
    QVERIFY (user_agent.contains (')'));
    QVERIFY (!user_agent.contains ('\n'));
    QVERIFY (!user_agent.contains ('\r'));
  }
};

QTEST_GUILESS_MAIN (TestHttpUserAgent)

#include "test_http_user_agent.moc"
