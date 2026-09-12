// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QVBoxLayout>
#include <QTest>
#include "widgets/CompactButtonSize.hpp"
#include "widgets/CurrentPageHeightStackedWidget.hpp"
#include "widgets/TxDriveSlider.hpp"

class CompactControlsTest : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void currentStackPageHeight ()
  {
    CurrentPageHeightStackedWidget stack;
    auto makePage = [&stack] (int rows)
      {
        auto * page = new QWidget {&stack};
        auto * layout = new QVBoxLayout {page};
        for (int row = 0; row < rows; ++row)
          layout->addWidget (new QPushButton {QString::number (row), page});
        return page;
      };
    auto * shortPage = makePage (2);
    auto * tallPage = makePage (6);
    stack.addWidget (shortPage);
    stack.addWidget (tallPage);
    stack.ensurePolished ();

    stack.setCurrentWidget (shortPage);
    QCOMPARE (stack.sizeHint ().height (), shortPage->sizeHint ().height ());
    QVERIFY (stack.sizeHint ().height () < tallPage->sizeHint ().height ());

    stack.setCurrentWidget (tallPage);
    QCOMPARE (stack.sizeHint ().height (), tallPage->sizeHint ().height ());
  }

  void nativeSlider ()
  {
#ifndef Q_OS_WIN
    QSKIP ("Native Windows slider contract");
#else
    for (int width : {27, 44})
      for (int value : {0, 225, 450})
        {
          TxDriveSlider actual;
          QSlider reference;
          reference.setTickPosition (QSlider::TicksRight);
          reference.setTickInterval (50);
          for (auto * slider : {static_cast<QSlider *> (&actual), &reference})
            {
              slider->setOrientation (Qt::Vertical);
              slider->setRange (0, 450);
              slider->setInvertedAppearance (true);
              slider->setValue (value);
              slider->resize (width, 200);
              slider->ensurePolished ();
            }
          QCOMPARE (actual.tickPosition (), reference.tickPosition ());
          QCOMPARE (actual.sizeHint (), reference.sizeHint ());
          QCOMPARE (actual.minimumSizeHint (), reference.minimumSizeHint ());
          QCOMPARE (actual.grab ().toImage (), reference.grab ().toImage ());
        }
#endif
  }
  void compactButtons ()
  {
#if defined (Q_OS_WIN)
    auto const families = {QStringLiteral ("Arial"), QStringLiteral ("Segoe UI")};
#elif defined (Q_OS_LINUX)
    auto const families = {QStringLiteral ("DejaVu Sans"), QStringLiteral ("Liberation Sans")};
#else
    QSKIP ("Compact Windows and Linux button contract");
#endif
#if defined (Q_OS_WIN) || defined (Q_OS_LINUX)
    for (auto const& family : families)
      for (int points : {8, 10, 13, 16})
        {
          QPushButton button {QStringLiteral ("Tx &1")};
          button.setFont (QFont {family, points});
          button.ensurePolished ();
          auto shortSize = compactButtonSize (&button);
          QVERIFY (shortSize.width () < button.sizeHint ().width ());
          button.resize (shortSize);
          QStyleOptionButton option;
          option.initFrom (&button);
          auto contents = button.style ()->subElementRect (QStyle::SE_PushButtonContents, &option, &button);
          auto text = button.fontMetrics ().size (Qt::TextShowMnemonic, button.text ());
          QVERIFY (contents.width () >= text.width ());
          QVERIFY (contents.height () >= text.height ());
          for (auto const& label : {QStringLiteral ("&Lookup"),
                                    QStringLiteral ("Add"),
                                    QStringLiteral ("Ignore")})
            {
              button.setText (label);
              button.resize (compactButtonSize (&button));
              option.initFrom (&button);
              contents = button.style ()->subElementRect (
                QStyle::SE_PushButtonContents, &option, &button);
              text = button.fontMetrics ().size (
                Qt::TextShowMnemonic, button.text ());
              QVERIFY (contents.width () >= text.width ());
              QVERIFY (contents.height () >= text.height ());
            }
          button.setText (QStringLiteral ("Translated action label"));
          QVERIFY (compactButtonSize (&button).width () > shortSize.width ());
        }
#endif
  }
};
QTEST_MAIN (CompactControlsTest)
#include "test_mainwindow_compact_controls.moc"
