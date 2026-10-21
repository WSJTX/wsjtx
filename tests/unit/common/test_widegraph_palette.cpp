// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QMessageBox>
#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include "L10nLoader.hpp"
#include "Logger.hpp"
#include "commons.h"
#include "widgets/widegraph.h"

namespace
{
  dec_data_t decoder_data {};
}
dec_data_t& dec_data = decoder_data;
QVector<QColor> g_ColorTbl;

class TestWideGraphPalette final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ()
  {
    Logger::disable ();
    qRegisterMetaTypeStreamOperators<WFPalette::Colours> ("WFPalette::Colours");
  }

  void restoresPaletteAcrossLanguages_data ()
  {
    QTest::addColumn<QString> ("saved_palette");
    QTest::addColumn<QString> ("language");
    QTest::addColumn<QString> ("expected_id");
    QTest::newRow ("legacy Hungarian to English") << "Hungarian custom palette" << "en" << "User Defined";
    QTest::newRow ("legacy German to Hungarian") << "German custom palette" << "hu" << "User Defined";
    QTest::newRow ("English to German") << "User Defined" << "de" << "User Defined";
    QTest::newRow ("named palette to Hungarian") << "Digipan" << "hu" << "Digipan";
  }

  void restoresPaletteAcrossLanguages ()
  {
    QFETCH (QString, saved_palette);
    QFETCH (QString, language);
    QFETCH (QString, expected_id);
    L10nLoader loader {qApp, QLocale {"en_US"}, language};
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const file = directory.filePath ("settings.ini");
    WFPalette::Colours const colours {QColor {10, 20, 30}, QColor {100, 150, 200}};
    {
      QSettings settings {file, QSettings::IniFormat};
      settings.setValue ("WideGraph/WaterfallPalette", saved_palette);
      settings.setValue ("WideGraph/UserPalette", QVariant::fromValue (colours));
    }

    for (int launch = 0; launch < 2; ++launch)
      {
        QSettings settings {file, QSettings::IniFormat};
        QStringList warnings;
        QTimer dismiss_warnings;
        connect (&dismiss_warnings, &QTimer::timeout, [&warnings] {
            if (auto box = qobject_cast<QMessageBox *> (QApplication::activeModalWidget ()))
              {
                warnings.append (box->text ());
                box->accept ();
              }
          });
        dismiss_warnings.start (0);
        WideGraph window {&settings};
        dismiss_warnings.stop ();
        QVERIFY2 (warnings.isEmpty (), qPrintable (warnings.join ('\n')));
        auto combo = window.findChild<QComboBox *> ("paletteComboBox");
        QVERIFY (combo);
        QCOMPARE (combo->currentData ().toString (), expected_id);
        if (expected_id == "User Defined")
          {
            QCOMPARE (combo->currentText (), QCoreApplication::translate ("WideGraph", "User Defined"));
            QCOMPARE (g_ColorTbl, WFPalette {colours}.interpolate ());
          }
        else
          QCOMPARE (g_ColorTbl, WFPalette {":/Palettes/Digipan.pal"}.interpolate ());
        window.saveSettings ();
        QCOMPARE (settings.value ("WideGraph/WaterfallPalette").toString (), expected_id);
        QCOMPARE (settings.value ("WideGraph/UserPalette").value<WFPalette::Colours> (), colours);
      }
  }

  void selectionUsesStableItemData ()
  {
    L10nLoader loader {qApp, QLocale {"en_US"}, "hu"};
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    WideGraph window {&settings};
    auto combo = window.findChild<QComboBox *> ("paletteComboBox");
    QVERIFY (combo);
    for (auto const& palette : {"User Defined", "Digipan"})
      {
        auto const index = combo->findData (QString {palette});
        QVERIFY (index >= 0);
        combo->setItemText (index, "Changed display label");
        combo->setCurrentIndex (index);
        QVERIFY (QMetaObject::invokeMethod (combo, "activated", Qt::DirectConnection, Q_ARG (int, index)));
        window.saveSettings ();
        QCOMPARE (settings.value ("WideGraph/WaterfallPalette").toString (), QString {palette});
      }
  }
};

QTEST_MAIN (TestWideGraphPalette)
#include "test_widegraph_palette.moc"
