#include "JttyMacroSets.hpp"

#include <QSettings>

#include <algorithm>

namespace Jtty
{
  namespace
  {
    QString const groupName {QStringLiteral ("JttyMacroSets")};

    MacroSet makeSet (QString const& name, QString const& builtIn, EnterRole enterRole,
                      std::initializer_list<Macro> macros)
    {
      MacroSet set;
      set.name = name;
      set.builtIn = builtIn;
      set.enterRole = enterRole;
      int i = 0;
      for (auto const& macro : macros) set.macros[i++] = macro;
      return set;
    }

    QString enterRoleKey (EnterRole role)
    {
      switch (role) {
      case EnterRole::Run: return QStringLiteral ("Run");
      case EnterRole::SearchAndPounce: return QStringLiteral ("S&P");
      case EnterRole::Off: break;
      }
      return QStringLiteral ("Off");
    }

    EnterRole enterRoleFromKey (QString const& key, EnterRole fallback)
    {
      if (key == QStringLiteral ("Run")) return EnterRole::Run;
      if (key == QStringLiteral ("S&P")) return EnterRole::SearchAndPounce;
      if (key == QStringLiteral ("Off")) return EnterRole::Off;
      return fallback;
    }
  }

  MacroSet defaultMacroSet (QString const& builtIn)
  {
    if (builtIn == QStringLiteral ("Run")) {
      return makeSet (builtIn, builtIn, EnterRole::Run, {
          {QStringLiteral ("CQ"), QStringLiteral ("%[CQ %M CQ%]")},
          {QStringLiteral ("Call Exch"), QStringLiteral ("%[%H %E%]")},
          {QStringLiteral ("TU CQ"), QStringLiteral ("%[%H TU CQ %M CQ%L%]")},
          {QStringLiteral ("My call"), QStringLiteral ("%[%M%]")},
          {QStringLiteral ("His call"), QStringLiteral ("%[%H%]")},
          {QStringLiteral ("TU Now"), QStringLiteral ("%[TU NOW %Q %E%L%]")},
          {QStringLiteral ("AGN?"), QStringLiteral ("%[%H AGN?%]")},
          {QStringLiteral ("Exch"), QStringLiteral ("%[%E%]")},
        });
    }
    if (builtIn == QStringLiteral ("S&P")) {
      return makeSet (builtIn, builtIn, EnterRole::SearchAndPounce, {
          {QStringLiteral ("My call"), QStringLiteral ("%[%M%]")},
          {QStringLiteral ("Call Exch"), QStringLiteral ("%[%H %E%]")},
          {QStringLiteral ("TU"), QStringLiteral ("%[TU %M%L%]")},
          {QStringLiteral ("My call"), QStringLiteral ("%[%M%]")},
          {QStringLiteral ("His call"), QStringLiteral ("%[%H%]")},
          {},
          {QStringLiteral ("AGN?"), QStringLiteral ("%[AGN?%]")},
          {QStringLiteral ("Exch"), QStringLiteral ("%[%E%]")},
        });
    }
    if (builtIn == QStringLiteral ("Chat")) {
      return makeSet (builtIn, builtIn, EnterRole::Off, {
          {QStringLiteral ("CQ"), QStringLiteral ("%[CQ CQ CQ DE %M %M K%]")},
          {QStringLiteral ("Open"), QStringLiteral ("%[%H DE %M ")},
          {QStringLiteral ("Over"), QStringLiteral ("BTU %H DE %M K%]")},
          {QStringLiteral ("Sign off"), QStringLiteral ("%H DE %M SK%]")},
        });
    }
    return {};
  }

  QVector<MacroSet> defaultMacroSets ()
  {
    return {defaultMacroSet (QStringLiteral ("Run")),
            defaultMacroSet (QStringLiteral ("S&P")),
            defaultMacroSet (QStringLiteral ("Chat"))};
  }

  QString macroKeyName (int key)
  {
    if (key < 1 || key > macroKeyCount) return {};
    return key <= 8 ? QStringLiteral ("F%1").arg (key)
                    : QStringLiteral ("Shift+F%1").arg (key - 8);
  }

  int enterSendsKey (EnterRole role, EnterStep step, bool haveDxCall)
  {
    switch (role) {
    case EnterRole::Off:
      return 0;
    case EnterRole::Run:
      if (!haveDxCall) return 1;
      switch (step) {
      case EnterStep::Idle:
      case EnterStep::CallSent: return 2;
      case EnterStep::ExchangeSent: return 3;
      case EnterStep::Done: return 1;
      }
      return 0;
    case EnterRole::SearchAndPounce:
      if (!haveDxCall) return 0;
      switch (step) {
      case EnterStep::Idle: return 4;
      case EnterStep::CallSent: return 2;
      case EnterStep::ExchangeSent: return 3;
      case EnterStep::Done: return 0;
      }
      return 0;
    }
    return 0;
  }

  EnterStep stepAfterKey (EnterRole role, int key, EnterStep step)
  {
    if (role == EnterRole::Off) return step;
    switch (key) {
    case 1: return role == EnterRole::Run ? EnterStep::Idle : step;
    case 2: return EnterStep::ExchangeSent;
    case 3: return EnterStep::Done;
    case 4: return role == EnterRole::SearchAndPounce ? EnterStep::CallSent : step;
    default: return step;
    }
  }

  MacroLibrary::MacroLibrary ()
    : m_sets {defaultMacroSets ()}
  {
  }

  Macro MacroLibrary::macro (int key) const
  {
    if (key < 1 || key > macroKeyCount) return {};
    return current ().macros[key - 1];
  }

  void MacroLibrary::setCurrentIndex (int index)
  {
    if (index >= 0 && index < m_sets.size ()) m_current = index;
  }

  bool MacroLibrary::selectByName (QString const& name)
  {
    for (int i = 0; i < m_sets.size (); ++i) {
      if (m_sets[i].name == name) {
        m_current = i;
        return true;
      }
    }
    return false;
  }

  void MacroLibrary::setMacro (int setIndex, int key, Macro const& macro)
  {
    if (setIndex < 0 || setIndex >= m_sets.size () || key < 1 || key > macroKeyCount) return;
    m_sets[setIndex].macros[key - 1] = macro;
  }

  void MacroLibrary::setEnterRole (int setIndex, EnterRole role)
  {
    if (setIndex >= 0 && setIndex < m_sets.size ()) m_sets[setIndex].enterRole = role;
  }

  QString MacroLibrary::uniqueName (QString const& base) const
  {
    QString const stem = base.simplified ().isEmpty () ? QStringLiteral ("My set")
                                                       : base.simplified ();
    auto const taken = [this] (QString const& name) {
      for (auto const& set : m_sets) {
        if (set.name.compare (name, Qt::CaseInsensitive) == 0) return true;
      }
      return false;
    };
    if (!taken (stem)) return stem;
    for (int n = 2; ; ++n) {
      QString const candidate = QStringLiteral ("%1 %2").arg (stem).arg (n);
      if (!taken (candidate)) return candidate;
    }
  }

  int MacroLibrary::addUserSet (QString const& name, int copyFrom)
  {
    MacroSet set;
    if (copyFrom >= 0 && copyFrom < m_sets.size ()) set = m_sets[copyFrom];
    set.name = uniqueName (name);
    set.builtIn.clear ();
    m_sets.append (set);
    return m_sets.size () - 1;
  }

  bool MacroLibrary::renameSet (int index, QString const& name)
  {
    if (index < 0 || index >= m_sets.size () || m_sets[index].isBuiltIn ()) return false;
    QString const trimmed = name.simplified ();
    if (trimmed.isEmpty ()) return false;
    for (int i = 0; i < m_sets.size (); ++i) {
      if (i != index && m_sets[i].name.compare (trimmed, Qt::CaseInsensitive) == 0) return false;
    }
    m_sets[index].name = trimmed;
    return true;
  }

  bool MacroLibrary::removeSet (int index)
  {
    if (index < 0 || index >= m_sets.size () || m_sets[index].isBuiltIn ()) return false;
    m_sets.removeAt (index);
    if (m_current == index) m_current = 0;
    else if (m_current > index) --m_current;
    return true;
  }

  bool MacroLibrary::restoreDefaults (int index)
  {
    if (index < 0 || index >= m_sets.size () || !m_sets[index].isBuiltIn ()) return false;
    auto const defaults = defaultMacroSet (m_sets[index].builtIn);
    m_sets[index].macros = defaults.macros;
    m_sets[index].enterRole = defaults.enterRole;
    return true;
  }

  void MacroLibrary::load (QSettings& settings)
  {
    QVector<MacroSet> loaded;
    settings.beginGroup (groupName);
    int const count = settings.beginReadArray (QStringLiteral ("Sets"));
    for (int i = 0; i < count; ++i) {
      settings.setArrayIndex (i);
      MacroSet set;
      set.name = settings.value (QStringLiteral ("Name")).toString ().simplified ();
      set.builtIn = settings.value (QStringLiteral ("BuiltIn")).toString ();
      if (set.isBuiltIn () && defaultMacroSet (set.builtIn).name.isEmpty ()) continue;
      if (set.isBuiltIn ()) set.name = set.builtIn;
      set.enterRole = enterRoleFromKey (settings.value (QStringLiteral ("Enter")).toString (),
                                        defaultMacroSet (set.builtIn).enterRole);
      if (set.name.isEmpty ()) continue;
      for (int key = 1; key <= macroKeyCount; ++key) {
        set.macros[key - 1].label = settings.value (QStringLiteral ("Label%1").arg (key)).toString ();
        set.macros[key - 1].text = settings.value (QStringLiteral ("Text%1").arg (key)).toString ();
      }
      bool duplicate = false;
      for (auto const& other : loaded) {
        if (other.name.compare (set.name, Qt::CaseInsensitive) == 0) duplicate = true;
      }
      if (!duplicate) loaded.append (set);
    }
    settings.endArray ();
    QString const currentName = settings.value (QStringLiteral ("Current")).toString ();
    settings.endGroup ();

    // Built-in sets first, in their usual order, then user sets as saved.
    QVector<MacroSet> sets;
    for (auto const& builtIn : defaultMacroSets ()) {
      auto found = std::find_if (loaded.cbegin (), loaded.cend (), [&] (MacroSet const& set) {
        return set.builtIn == builtIn.builtIn;
      });
      sets.append (found != loaded.cend () ? *found : builtIn);
    }
    for (auto const& set : loaded) {
      if (!set.isBuiltIn ()) sets.append (set);
    }
    m_sets = sets;
    m_current = 0;
    selectByName (currentName);
  }

  void MacroLibrary::save (QSettings& settings) const
  {
    settings.beginGroup (groupName);
    settings.remove (QString {});
    settings.beginWriteArray (QStringLiteral ("Sets"), m_sets.size ());
    for (int i = 0; i < m_sets.size (); ++i) {
      settings.setArrayIndex (i);
      auto const& set = m_sets[i];
      settings.setValue (QStringLiteral ("Name"), set.name);
      settings.setValue (QStringLiteral ("BuiltIn"), set.builtIn);
      settings.setValue (QStringLiteral ("Enter"), enterRoleKey (set.enterRole));
      for (int key = 1; key <= macroKeyCount; ++key) {
        settings.setValue (QStringLiteral ("Label%1").arg (key), set.macros[key - 1].label);
        settings.setValue (QStringLiteral ("Text%1").arg (key), set.macros[key - 1].text);
      }
    }
    settings.endArray ();
    settings.setValue (QStringLiteral ("Current"), current ().name);
    settings.endGroup ();
  }
}
