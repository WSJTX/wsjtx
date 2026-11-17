// The JTTY style buttons are defined in mainwindow.ui, not built in C++: this
// reads the .ui and checks they are what the C++ (one buttonClicked handler,
// refreshJttyMacroControls) relies on.
#include <QFile>
#include <QMap>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtTest>

#include "widgets/JttyMacroSets.hpp"

namespace
{
  struct Widget
  {
    QString name;
    QString cls;
    QString layout;               // innermost layout that holds it
    QMap<QString, QString> props; // property name -> first value text
    QMap<QString, QString> attrs;
  };

  QVector<Widget> readWidgets (QString const& path)
  {
    QVector<Widget> widgets;
    QFile file {path};
    if (!file.open (QIODevice::ReadOnly)) return widgets;
    QXmlStreamReader xml {&file};
    QStringList layouts;
    QVector<int> open;        // indexes of widgets being read
    QString propName, attrName;
    bool inProp = false, inAttr = false;
    while (!xml.atEnd ()) {
      xml.readNext ();
      if (xml.isStartElement ()) {
        auto const tag = xml.name ().toString ();
        if (tag == QLatin1String ("layout")) {
          layouts << xml.attributes ().value (QStringLiteral ("name")).toString ();
        } else if (tag == QLatin1String ("widget")) {
          Widget w;
          w.name = xml.attributes ().value (QStringLiteral ("name")).toString ();
          w.cls = xml.attributes ().value (QStringLiteral ("class")).toString ();
          w.layout = layouts.isEmpty () ? QString {} : layouts.last ();
          widgets.append (w);
          open << widgets.size () - 1;
        } else if (tag == QLatin1String ("property") && !open.isEmpty ()) {
          inProp = true;
          propName = xml.attributes ().value (QStringLiteral ("name")).toString ();
        } else if (tag == QLatin1String ("attribute") && !open.isEmpty ()) {
          inAttr = true;
          attrName = xml.attributes ().value (QStringLiteral ("name")).toString ();
        } else if (!open.isEmpty () && (inProp || inAttr)
                   && (tag == QLatin1String ("string") || tag == QLatin1String ("bool")
                       || tag == QLatin1String ("enum"))) {
          auto const value = xml.readElementText ();
          auto& w = widgets[open.last ()];
          if (inProp && !w.props.contains (propName)) w.props[propName] = value;
          if (inAttr) w.attrs[attrName] = value;
        }
      } else if (xml.isEndElement ()) {
        auto const tag = xml.name ().toString ();
        if (tag == QLatin1String ("layout") && !layouts.isEmpty ()) layouts.removeLast ();
        else if (tag == QLatin1String ("widget") && !open.isEmpty ()) open.removeLast ();
        else if (tag == QLatin1String ("property")) inProp = false;
        else if (tag == QLatin1String ("attribute")) inAttr = false;
      }
    }
    return widgets;
  }
}

class TestJttyStyleButtons final
  : public QObject
{
  Q_OBJECT

private slots:

  void oneCheckableButtonPerBuiltInSetInAGroupAndALayout ()
  {
    auto const widgets = readWidgets (QStringLiteral (WSJTX_SOURCE_DIR "/widgets/mainwindow.ui"));
    QVERIFY (!widgets.isEmpty ());

    QVector<Widget> buttons;
    QStringList names;
    for (auto const& w : widgets) {
      QVERIFY2 (w.name != QLatin1String ("comboBoxJttyStyle"), "the drop-down is gone");
      QVERIFY2 (w.name != QLatin1String ("label_jttyStyle"), "its label is gone");
      if (w.cls == QLatin1String ("QPushButton") && w.attrs.contains (QStringLiteral ("buttonGroup"))
          && w.attrs[QStringLiteral ("buttonGroup")] == QLatin1String ("buttonGroupJttyStyle")) {
        buttons << w;
        names << w.props.value (QStringLiteral ("macroSet"));
      }
    }
    // Same names, same order as the built-in macro sets.
    QStringList expected;
    for (auto const& set : Jtty::defaultMacroSets ()) expected << set.name;
    QCOMPARE (names, expected);

    for (auto const& b : buttons) {
      QCOMPARE (b.props.value (QStringLiteral ("checkable")), QStringLiteral ("true"));
      QCOMPARE (b.props.value (QStringLiteral ("focusPolicy")), QStringLiteral ("Qt::NoFocus"));
      QVERIFY (b.props.value (QStringLiteral ("styleSheet")).contains (QStringLiteral (":checked")));
      QVERIFY (!b.props.value (QStringLiteral ("toolTip")).isEmpty ());
      QCOMPARE (b.layout, QStringLiteral ("horizontalLayout_jttyStyle"));
    }
    // S&P is shown with a doubled ampersand, so it reads S&P and not SP.
    QCOMPARE (buttons[1].props.value (QStringLiteral ("text")), QStringLiteral ("S&&P"));

    // Edit macros stays in the same row, after the style buttons.
    int edit = -1, last = -1;
    for (int i = 0; i < widgets.size (); ++i) {
      if (widgets[i].name == QLatin1String ("pbEditJttyMacros")) edit = i;
      if (widgets[i].name == buttons.last ().name) last = i;
    }
    QVERIFY (edit > last && last >= 0);
    QCOMPARE (widgets[edit].layout, QStringLiteral ("horizontalLayout_jttyStyle"));
  }

  void theMyCallPrefixBoxSitsInTheJttyGrid ()
  {
    // It once ended up in another page's grid and was never shown.
    auto const widgets = readWidgets (QStringLiteral (WSJTX_SOURCE_DIR "/widgets/mainwindow.ui"));
    QString prefix, serial, test;
    int count = 0;
    for (auto const& w : widgets) {
      if (w.name == QLatin1String ("cbJttyMyCallPrefix")) { prefix = w.layout; ++count; }
      if (w.name == QLatin1String ("sbSerialNumber_2")) serial = w.layout;
      if (w.name == QLatin1String ("pbJttyTest")) test = w.layout;
    }
    QCOMPARE (count, 1);
    QVERIFY (!serial.isEmpty ());
    QCOMPARE (prefix, serial);
    QCOMPARE (test, serial);   // the temporary Test button, same row
  }

  void theGroupIsDeclaredInTheUiFile ()
  {
    QFile file {QStringLiteral (WSJTX_SOURCE_DIR "/widgets/mainwindow.ui")};
    QVERIFY (file.open (QIODevice::ReadOnly));
    auto const text = QString::fromUtf8 (file.readAll ());
    QVERIFY (text.contains (QStringLiteral ("<buttongroup name=\"buttonGroupJttyStyle\"")));
  }
};

QTEST_GUILESS_MAIN (TestJttyStyleButtons)
#include "test_jtty_style_buttons.moc"