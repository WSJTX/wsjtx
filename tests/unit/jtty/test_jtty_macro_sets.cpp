#include <QtTest>

#include <QSettings>
#include <QTemporaryDir>

#include "widgets/JttyMacroSets.hpp"
#include "widgets/JttyMessages.hpp"
#include "widgets/JttyTransmitText.hpp"

class TestJttyMacroSets final
  : public QObject
{
  Q_OBJECT

  QTemporaryDir m_dir;

  QString settingsPath (QString const& name) const
  {
    return m_dir.filePath (name + QStringLiteral (".ini"));
  }

private slots:

  void builtInSetsComeFirstInOrder ()
  {
    Jtty::MacroLibrary const library;
    QCOMPARE (library.sets ().size (), 3);
    QCOMPARE (library.sets ()[0].name, QStringLiteral ("Run"));
    QCOMPARE (library.sets ()[1].name, QStringLiteral ("S&P"));
    QCOMPARE (library.sets ()[2].name, QStringLiteral ("Chat"));
    for (auto const& set : library.sets ()) QVERIFY (set.isBuiltIn ());
    QCOMPARE (library.currentIndex (), 0);
  }

  void runSetIsTheNativeFormsWrappedInPtt ()
  {
    auto const run = Jtty::defaultMacroSet (QStringLiteral ("Run"));
    for (int key = 1; key <= 8; ++key) {
      auto const control = Jtty::parseMacroControl (run.macros[key - 1].text);
      QCOMPARE (control.text, Jtty::nativeMacroTemplate (key));
      QVERIFY (control.pttOn);
      QVERIFY (control.pttOff);
      QCOMPARE (control.logQso, key == 3 || key == 6);
      QCOMPARE (control.startsNextQso (), key == 6);
      QVERIFY (!run.macros[key - 1].label.isEmpty ());
    }
    for (int key = 9; key <= Jtty::macroKeyCount; ++key) {
      QVERIFY (run.macros[key - 1].text.isEmpty ());
    }
  }

  void chatOpenerLeavesPttOn ()
  {
    auto const chat = Jtty::defaultMacroSet (QStringLiteral ("Chat"));
    auto const open = Jtty::parseMacroControl (chat.macros[1].text);
    QVERIFY (open.pttOn);
    QVERIFY (!open.pttOff);
    auto const over = Jtty::parseMacroControl (chat.macros[2].text);
    QVERIFY (!over.pttOn);
    QVERIFY (over.pttOff);
  }

  void enterRolesOfTheBuiltInSets ()
  {
    QCOMPARE (Jtty::defaultMacroSet (QStringLiteral ("Run")).enterRole, Jtty::EnterRole::Run);
    QCOMPARE (Jtty::defaultMacroSet (QStringLiteral ("S&P")).enterRole,
              Jtty::EnterRole::SearchAndPounce);
    QCOMPARE (Jtty::defaultMacroSet (QStringLiteral ("Chat")).enterRole, Jtty::EnterRole::Off);
    // Logging only at F3 (TU) and, in Run, F6 (TU NOW, which opens the next QSO).
    for (QString const& name : {QStringLiteral ("Run"), QStringLiteral ("S&P")}) {
      auto const set = Jtty::defaultMacroSet (name);
      bool const run = name == QStringLiteral ("Run");
      for (int key = 1; key <= Jtty::macroKeyCount; ++key) {
        QCOMPARE (Jtty::parseMacroControl (set.macros[key - 1].text).logQso,
                  key == 3 || (run && key == 6));
      }
    }
  }

  // Each step: what Enter sends, then the step that follows.
  void runEnterSequence ()
  {
    using Jtty::EnterStep;
    auto const run = Jtty::EnterRole::Run;
    QCOMPARE (Jtty::enterSendsKey (run, EnterStep::Idle, false), 1);
    QCOMPARE (Jtty::stepAfterKey (run, 1, EnterStep::Idle), EnterStep::Idle);
    QCOMPARE (Jtty::enterSendsKey (run, EnterStep::Idle, true), 2);
    QCOMPARE (Jtty::stepAfterKey (run, 2, EnterStep::Idle), EnterStep::ExchangeSent);
    QCOMPARE (Jtty::enterSendsKey (run, EnterStep::ExchangeSent, true), 3);
    QCOMPARE (Jtty::stepAfterKey (run, 3, EnterStep::ExchangeSent), EnterStep::Done);
    // DX Call not cleared after logging: back to CQ.
    QCOMPARE (Jtty::enterSendsKey (run, EnterStep::Done, true), 1);
    // AGN? and the like leave the step alone.
    QCOMPARE (Jtty::stepAfterKey (run, 7, EnterStep::ExchangeSent), EnterStep::ExchangeSent);
    QCOMPARE (Jtty::stepAfterKey (run, 4, EnterStep::ExchangeSent), EnterStep::ExchangeSent);
  }

  void searchAndPounceEnterSequence ()
  {
    using Jtty::EnterStep;
    auto const sp = Jtty::EnterRole::SearchAndPounce;
    QCOMPARE (Jtty::enterSendsKey (sp, EnterStep::Idle, false), 0);
    QCOMPARE (Jtty::enterSendsKey (sp, EnterStep::Idle, true), 4);
    QCOMPARE (Jtty::stepAfterKey (sp, 4, EnterStep::Idle), EnterStep::CallSent);
    QCOMPARE (Jtty::enterSendsKey (sp, EnterStep::CallSent, true), 2);
    QCOMPARE (Jtty::stepAfterKey (sp, 2, EnterStep::CallSent), EnterStep::ExchangeSent);
    QCOMPARE (Jtty::enterSendsKey (sp, EnterStep::ExchangeSent, true), 3);
    QCOMPARE (Jtty::stepAfterKey (sp, 3, EnterStep::ExchangeSent), EnterStep::Done);
    QCOMPARE (Jtty::enterSendsKey (sp, EnterStep::Done, true), 0);
    QCOMPARE (Jtty::stepAfterKey (sp, 1, EnterStep::CallSent), EnterStep::CallSent);
  }

  void enterOffSendsNothing ()
  {
    using Jtty::EnterStep;
    auto const off = Jtty::EnterRole::Off;
    for (bool haveCall : {false, true}) {
      QCOMPARE (Jtty::enterSendsKey (off, EnterStep::Idle, haveCall), 0);
    }
    QCOMPARE (Jtty::stepAfterKey (off, 2, EnterStep::Idle), EnterStep::Idle);
  }

  void enterRoleIsCopiedSavedAndRestored ()
  {
    Jtty::MacroLibrary library;
    int const user = library.addUserSet (QStringLiteral ("Sprint"), 1);
    QCOMPARE (library.sets ()[user].enterRole, Jtty::EnterRole::SearchAndPounce);
    library.setEnterRole (user, Jtty::EnterRole::Run);
    library.setEnterRole (0, Jtty::EnterRole::Off);
    {
      QSettings settings {settingsPath (QStringLiteral ("enter")), QSettings::IniFormat};
      library.save (settings);
    }
    QSettings settings {settingsPath (QStringLiteral ("enter")), QSettings::IniFormat};
    Jtty::MacroLibrary loaded;
    loaded.load (settings);
    QCOMPARE (loaded.sets ()[0].enterRole, Jtty::EnterRole::Off);
    QCOMPARE (loaded.sets ()[user].enterRole, Jtty::EnterRole::Run);
    QVERIFY (loaded.restoreDefaults (0));
    QCOMPARE (loaded.sets ()[0].enterRole, Jtty::EnterRole::Run);
  }

  void keyNames ()
  {
    QCOMPARE (Jtty::macroKeyName (1), QStringLiteral ("F1"));
    QCOMPARE (Jtty::macroKeyName (8), QStringLiteral ("F8"));
    QCOMPARE (Jtty::macroKeyName (9), QStringLiteral ("Shift+F1"));
    QCOMPARE (Jtty::macroKeyName (16), QStringLiteral ("Shift+F8"));
    QVERIFY (Jtty::macroKeyName (0).isEmpty ());
    QVERIFY (Jtty::macroKeyName (17).isEmpty ());
  }

  void builtInSetsCannotBeRenamedOrRemoved ()
  {
    Jtty::MacroLibrary library;
    QVERIFY (!library.renameSet (0, QStringLiteral ("Mine")));
    QVERIFY (!library.removeSet (1));
    QCOMPARE (library.sets ().size (), 3);
  }

  void userSetsCopyRenameAndRemove ()
  {
    Jtty::MacroLibrary library;
    int const index = library.addUserSet (QStringLiteral ("Sprint"), 0);
    QCOMPARE (index, 3);
    auto const& added = library.sets ()[index];
    QVERIFY (!added.isBuiltIn ());
    QCOMPARE (added.name, QStringLiteral ("Sprint"));
    QVERIFY (added.macros == library.sets ()[0].macros);

    QCOMPARE (library.uniqueName (QStringLiteral ("sprint")), QStringLiteral ("sprint 2"));
    QCOMPARE (library.sets ()[library.addUserSet (QStringLiteral ("Sprint"), 0)].name,
              QStringLiteral ("Sprint 2"));

    QVERIFY (!library.renameSet (index, QStringLiteral ("run")));   // case-insensitive clash
    QVERIFY (!library.renameSet (index, QStringLiteral ("   ")));
    QVERIFY (library.renameSet (index, QStringLiteral ("  NA  Sprint ")));
    QCOMPARE (library.sets ()[index].name, QStringLiteral ("NA Sprint"));

    library.setCurrentIndex (4);
    QVERIFY (library.removeSet (index));
    QCOMPARE (library.current ().name, QStringLiteral ("Sprint 2"));
    QVERIFY (library.removeSet (library.currentIndex ()));
    QCOMPARE (library.currentIndex (), 0);
  }

  void restoreDefaultsOnlyForBuiltIns ()
  {
    Jtty::MacroLibrary library;
    library.setMacro (0, 1, {QStringLiteral ("X"), QStringLiteral ("TEST")});
    QVERIFY (library.restoreDefaults (0));
    QVERIFY (library.sets ()[0].macros == Jtty::defaultMacroSet (QStringLiteral ("Run")).macros);
    int const user = library.addUserSet (QStringLiteral ("Mine"), 0);
    QVERIFY (!library.restoreDefaults (user));
  }

  void saveAndLoadRoundTrip ()
  {
    Jtty::MacroLibrary library;
    library.setMacro (0, 9, {QStringLiteral ("QRL?"), QStringLiteral ("%[QRL?%]")});
    int const user = library.addUserSet (QStringLiteral ("Sprint"), 2);
    library.setMacro (user, 16, {QStringLiteral ("Last"), QStringLiteral ("%H 73%]")});
    library.setCurrentIndex (user);
    {
      QSettings settings {settingsPath (QStringLiteral ("roundtrip")), QSettings::IniFormat};
      library.save (settings);
    }
    QSettings settings {settingsPath (QStringLiteral ("roundtrip")), QSettings::IniFormat};
    Jtty::MacroLibrary loaded;
    loaded.load (settings);
    QCOMPARE (loaded.sets ().size (), 4);
    QCOMPARE (loaded.current ().name, QStringLiteral ("Sprint"));
    for (int i = 0; i < 4; ++i) {
      QCOMPARE (loaded.sets ()[i].name, library.sets ()[i].name);
      QCOMPARE (loaded.sets ()[i].builtIn, library.sets ()[i].builtIn);
      QVERIFY (loaded.sets ()[i].macros == library.sets ()[i].macros);
    }
  }

  void loadWithNothingSavedGivesDefaults ()
  {
    QSettings settings {settingsPath (QStringLiteral ("empty")), QSettings::IniFormat};
    Jtty::MacroLibrary library;
    library.addUserSet (QStringLiteral ("Stale"), 0);
    library.load (settings);
    QCOMPARE (library.sets ().size (), 3);
    QVERIFY (library.sets ()[0].macros == Jtty::defaultMacroSet (QStringLiteral ("Run")).macros);
    QCOMPARE (library.currentIndex (), 0);
  }

  void loadRestoresMissingBuiltInsAndDropsBadEntries ()
  {
    QString const path = settingsPath (QStringLiteral ("partial"));
    {
      QSettings settings {path, QSettings::IniFormat};
      settings.beginGroup (QStringLiteral ("JttyMacroSets"));
      settings.beginWriteArray (QStringLiteral ("Sets"), 4);
      settings.setArrayIndex (0);
      settings.setValue (QStringLiteral ("Name"), QStringLiteral ("Mine"));
      settings.setValue (QStringLiteral ("Text1"), QStringLiteral ("%[CQ TEST %M%]"));
      settings.setArrayIndex (1);
      settings.setValue (QStringLiteral ("Name"), QStringLiteral ("Bogus"));
      settings.setValue (QStringLiteral ("BuiltIn"), QStringLiteral ("NoSuchSet"));
      settings.setArrayIndex (2);
      settings.setValue (QStringLiteral ("Name"), QString {});
      settings.setArrayIndex (3);
      settings.setValue (QStringLiteral ("Name"), QStringLiteral ("mine"));
      settings.endArray ();
      settings.setValue (QStringLiteral ("Current"), QStringLiteral ("Gone"));
      settings.endGroup ();
    }
    QSettings settings {path, QSettings::IniFormat};
    Jtty::MacroLibrary library;
    library.load (settings);
    QCOMPARE (library.sets ().size (), 4);
    QCOMPARE (library.sets ()[0].name, QStringLiteral ("Run"));
    QCOMPARE (library.sets ()[3].name, QStringLiteral ("Mine"));
    QCOMPARE (library.sets ()[3].macros[0].text, QStringLiteral ("%[CQ TEST %M%]"));
    QCOMPARE (library.currentIndex (), 0);
  }

  // F-keys send their expanded text through the Tx box rather than as native
  // atoms. Sent whole, the Run set must cost no more frames than those atoms.
  void runSetAsTextCostsNoMoreFramesThanNativeForms ()
  {
    struct Profile { Jtty::NativeExchangeProfile profile; QString exchange; };
    Profile const profiles[] {
      {Jtty::NativeExchangeProfile::None, {}},
      {Jtty::NativeExchangeProfile::RttyRoundup, QStringLiteral ("MA")},
      {Jtty::NativeExchangeProfile::FieldDay, QStringLiteral ("1A EMA")},
    };
    auto const run = Jtty::defaultMacroSet (QStringLiteral ("Run"));
    for (auto const& p : profiles) {
      auto const context = Jtty::nativeMacroContext (
        QStringLiteral ("K1ABC"), QStringLiteral ("W9XYZ"), 107, QStringLiteral ("FN42"), -7,
        p.profile, p.exchange);
      for (int key = 1; key <= 8; ++key) {
        auto const compiled = Jtty::compileNativeMacro (Jtty::nativeMacroTemplate (key), context);
        QVERIFY (compiled.isNative ());
        int const nativeFrames =
          Jtty::encodeNativeAtoms (compiled.atoms).tones.size () / Jtty::transmitFrameSymbols;

        auto const control = Jtty::parseMacroControl (run.macros[key - 1].text);
        auto const text = Jtty::expandLiteralMacro (control.text, context);
        auto const encoded = Jtty::encodeTransmitText (text, p.profile, true);
        QCOMPARE (encoded.status, Jtty::TransmitTextStatus::Encoded);
        int textFrames = 0;
        for (auto const& segment : encoded.segments) {
          textFrames += segment.transmit.tones.size () / Jtty::transmitFrameSymbols;
        }
        QVERIFY2 (textFrames <= nativeFrames,
                  qPrintable (QStringLiteral ("F%1 [%2]: %3 frames as text, %4 native")
                              .arg (key).arg (text).arg (textFrames).arg (nativeFrames)));
      }
    }
  }

  void everyDefaultMacroEncodes ()
  {
    auto const context = Jtty::nativeMacroContext (
      QStringLiteral ("K1ABC"), QStringLiteral ("W9XYZ"), 107, QStringLiteral ("FN42"), -7,
      Jtty::NativeExchangeProfile::None, {});
    for (auto const& set : Jtty::defaultMacroSets ()) {
      for (auto const& macro : set.macros) {
        if (macro.text.isEmpty ()) continue;
        auto const text = Jtty::expandLiteralMacro (Jtty::parseMacroControl (macro.text).text, context);
        QVERIFY2 (Jtty::encodeTransmitText (text, Jtty::NativeExchangeProfile::None, true).status
                  == Jtty::TransmitTextStatus::Encoded,
                  qPrintable (set.name + QStringLiteral (": ") + text));
      }
    }
  }
};

QTEST_GUILESS_MAIN (TestJttyMacroSets)
#include "test_jtty_macro_sets.moc"
