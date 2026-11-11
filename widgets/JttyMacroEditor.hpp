// -*- Mode: C++ -*-
#ifndef JTTY_MACRO_EDITOR_HPP_
#define JTTY_MACRO_EDITOR_HPP_

#include <QDialog>

#include "JttyMacroSets.hpp"

class QComboBox;
class QPushButton;
class QTableWidget;

// Edits a copy of the JTTY macro sets; library() holds the result after
// the dialog is accepted, with the set last shown selected as current.
class JttyMacroEditor final
  : public QDialog
{
  Q_OBJECT

public:
  explicit JttyMacroEditor (Jtty::MacroLibrary const& library, QWidget * parent = nullptr);

  Jtty::MacroLibrary const& library () const { return m_library; }

private:
  void populateSets ();
  void showSet (int index);
  void updateButtons ();
  void newSet ();
  void renameSet ();
  void deleteSet ();
  void restoreDefaults ();
  void storeCell (int row, int column);

  Jtty::MacroLibrary m_library;
  QComboBox * m_setCombo;
  QComboBox * m_enterCombo;
  QPushButton * m_renameButton;
  QPushButton * m_deleteButton;
  QPushButton * m_restoreButton;
  QTableWidget * m_table;
  bool m_populating {false};
};

#endif
