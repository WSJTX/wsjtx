// -*- Mode: C++ -*-
#ifndef JTTY_MACRO_SETS_HPP_
#define JTTY_MACRO_SETS_HPP_

#include <array>
#include <QString>
#include <QVector>

class QSettings;

namespace Jtty
{
  // F1-F8 and Shift+F1-F8.
  inline constexpr int macroKeyCount = 16;

  struct Macro
  {
    QString label;
    QString text;

    bool operator== (Macro const& other) const
    {
      return label == other.label && text == other.text;
    }
  };

  // What Enter does when Enter Sends Message is on: step through a contest
  // QSO (see enterSendsKey), or for Off act like the Send message button.
  enum class EnterRole { Off, Run, SearchAndPounce };

  struct MacroSet
  {
    QString name;
    // "Run", "S&P" or "Chat" for a built-in set, whose name is fixed and
    // whose contents can be restored; empty for a user set.
    QString builtIn;
    std::array<Macro, macroKeyCount> macros;
    EnterRole enterRole {EnterRole::Off};

    bool isBuiltIn () const { return !builtIn.isEmpty (); }
  };

  QVector<MacroSet> defaultMacroSets ();
  MacroSet defaultMacroSet (QString const& builtIn);

  // Keys are 1-based: 1-8 are F1-F8, 9-16 are Shift+F1-F8.
  QString macroKeyName (int key);

  // Enter Sends Message, as in N1MM: how far the QSO with the current DX
  // call has got, by which macro key was last sent to it.
  enum class EnterStep { Idle, CallSent, ExchangeSent, Done };

  // The macro key Enter sends next, or 0 for none.
  //   Run: no DX call F1 (CQ); then F2 (call + exchange); then F3 (TU, log).
  //   S&P: F4 (my call); then F2 (exchange); then F3 (TU, log).
  int enterSendsKey (EnterRole role, EnterStep step, bool haveDxCall);
  // The step after macro key 'key' was sent, however it was sent.
  EnterStep stepAfterKey (EnterRole role, int key, EnterStep step);

  class MacroLibrary
  {
  public:
    MacroLibrary ();

    QVector<MacroSet> const& sets () const { return m_sets; }
    int currentIndex () const { return m_current; }
    MacroSet const& current () const { return m_sets.at (m_current); }
    Macro macro (int key) const;

    void setCurrentIndex (int index);
    bool selectByName (QString const& name);

    void setMacro (int setIndex, int key, Macro const& macro);
    void setEnterRole (int setIndex, EnterRole role);

    // A name not already in use, made from base.
    QString uniqueName (QString const& base) const;

    // Copies setIndex under a fresh name, appended; returns the new index.
    int addUserSet (QString const& name, int copyFrom);
    // User sets only; the name must be non-empty and unused.
    bool renameSet (int index, QString const& name);
    // User sets only. The current selection stays on the same set, or falls
    // back to the first set if that one was removed.
    bool removeSet (int index);
    // Built-in sets only.
    bool restoreDefaults (int index);

    // Missing or unreadable settings give the defaults; built-in sets missing
    // from the settings are restored, so they are always present.
    void load (QSettings& settings);
    void save (QSettings& settings) const;

  private:
    QVector<MacroSet> m_sets;
    int m_current {0};
  };
}

#endif
