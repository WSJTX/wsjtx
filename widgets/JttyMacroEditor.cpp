#include "JttyMacroEditor.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

JttyMacroEditor::JttyMacroEditor (Jtty::MacroLibrary const& library, QWidget * parent)
  : QDialog {parent}
  , m_library {library}
  , m_setCombo {new QComboBox}
  , m_enterCombo {new QComboBox}
  , m_renameButton {new QPushButton {tr ("Rename...")}}
  , m_deleteButton {new QPushButton {tr ("Delete")}}
  , m_restoreButton {new QPushButton {tr ("Restore defaults")}}
  , m_table {new QTableWidget {Jtty::macroKeyCount, 3}}
{
  setWindowTitle (tr ("JTTY Macros"));

  auto * newButton = new QPushButton {tr ("New...")};
  newButton->setToolTip (tr ("Make a new set, starting from a copy of the one shown."));
  m_restoreButton->setToolTip (tr ("Put back this built-in set's original macros."));

  auto * setRow = new QHBoxLayout;
  setRow->addWidget (new QLabel {tr ("Macro set:")});
  setRow->addWidget (m_setCombo, 1);
  setRow->addWidget (newButton, 1);
  setRow->addWidget (m_renameButton, 1);
  setRow->addWidget (m_deleteButton, 1);
  setRow->addWidget (m_restoreButton, 1);

  m_table->setHorizontalHeaderLabels ({tr ("Key"), tr ("Label"), tr ("Macro")});
  m_table->verticalHeader ()->hide ();
  m_table->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Interactive);
  m_table->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::Stretch);
  m_table->setColumnWidth (1, 120);
  m_table->setSelectionMode (QAbstractItemView::SingleSelection);

  m_enterCombo->addItem (tr ("Run: F1 CQ, F2 exchange, F3 TU"),
                         int (Jtty::EnterRole::Run));
  m_enterCombo->addItem (tr ("S&P: F4 my call, F2 exchange, F3 TU"),
                         int (Jtty::EnterRole::SearchAndPounce));
  m_enterCombo->addItem (tr ("Send message (the Tx box)"), int (Jtty::EnterRole::Off));
  m_enterCombo->setToolTip (tr ("With Enter sends message checked, what Enter does while this set is in use: step through a contest QSO, or act like the Send message button."));
  auto * enterRow = new QHBoxLayout;
  enterRow->addWidget (new QLabel {tr ("Enter sends:")});
  enterRow->addWidget (m_enterCombo);
  enterRow->addStretch (1);

  auto * help = new QLabel {tr (
      "%M my call · %H DX call · %Q call next · %N serial number · "
      "%E exchange · %G grid · %R report · %[ PTT on · %] PTT off · %L log QSO")};
  help->setWordWrap (true);

  auto * buttons = new QDialogButtonBox {QDialogButtonBox::Ok | QDialogButtonBox::Cancel};

  auto * layout = new QVBoxLayout {this};
  layout->addLayout (setRow);
  layout->addWidget (m_table, 1);
  layout->addLayout (enterRow);
  layout->addWidget (help);
  layout->addWidget (buttons);
  resize (640, 560);

  connect (m_setCombo, QOverload<int>::of (&QComboBox::currentIndexChanged),
           this, &JttyMacroEditor::showSet);
  connect (newButton, &QPushButton::clicked, this, &JttyMacroEditor::newSet);
  connect (m_renameButton, &QPushButton::clicked, this, &JttyMacroEditor::renameSet);
  connect (m_deleteButton, &QPushButton::clicked, this, &JttyMacroEditor::deleteSet);
  connect (m_restoreButton, &QPushButton::clicked, this, &JttyMacroEditor::restoreDefaults);
  connect (m_table, &QTableWidget::cellChanged, this, &JttyMacroEditor::storeCell);
  connect (m_enterCombo, QOverload<int>::of (&QComboBox::currentIndexChanged), this, [this] (int i) {
    if (!m_populating)
      m_library.setEnterRole (m_library.currentIndex (),
                              Jtty::EnterRole (m_enterCombo->itemData (i).toInt ()));
  });
  connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  populateSets ();
}

void JttyMacroEditor::populateSets ()
{
  {
    QSignalBlocker const blocker {m_setCombo};
    m_setCombo->clear ();
    for (auto const& set : m_library.sets ()) m_setCombo->addItem (set.name);
    m_setCombo->setCurrentIndex (m_library.currentIndex ());
  }
  showSet (m_library.currentIndex ());
}

void JttyMacroEditor::showSet (int index)
{
  if (index < 0 || index >= m_library.sets ().size ()) return;
  m_library.setCurrentIndex (index);
  m_populating = true;
  auto const& set = m_library.current ();
  for (int key = 1; key <= Jtty::macroKeyCount; ++key) {
    auto * keyItem = new QTableWidgetItem {Jtty::macroKeyName (key)};
    keyItem->setFlags (Qt::ItemIsEnabled);
    m_table->setItem (key - 1, 0, keyItem);
    m_table->setItem (key - 1, 1, new QTableWidgetItem {set.macros[key - 1].label});
    m_table->setItem (key - 1, 2, new QTableWidgetItem {set.macros[key - 1].text});
  }
  m_enterCombo->setCurrentIndex (m_enterCombo->findData (int (set.enterRole)));
  m_populating = false;
  updateButtons ();
}

void JttyMacroEditor::updateButtons ()
{
  bool const builtIn = m_library.current ().isBuiltIn ();
  m_renameButton->setEnabled (!builtIn);
  m_deleteButton->setEnabled (!builtIn);
  m_restoreButton->setEnabled (builtIn);
}

void JttyMacroEditor::storeCell (int row, int column)
{
  if (m_populating || column < 1) return;
  int const key = row + 1;
  Jtty::Macro macro = m_library.macro (key);
  QString const value = m_table->item (row, column)->text ();
  if (column == 1) macro.label = value;
  else macro.text = value;
  m_library.setMacro (m_library.currentIndex (), key, macro);
}

void JttyMacroEditor::newSet ()
{
  bool ok = false;
  QString const name = QInputDialog::getText (
    this, tr ("New macro set"), tr ("Name:"), QLineEdit::Normal,
    m_library.uniqueName (tr ("My set")), &ok);
  if (!ok) return;
  int const index = m_library.addUserSet (name, m_library.currentIndex ());
  m_library.setCurrentIndex (index);
  populateSets ();
}

void JttyMacroEditor::renameSet ()
{
  bool ok = false;
  QString const name = QInputDialog::getText (
    this, tr ("Rename macro set"), tr ("Name:"), QLineEdit::Normal,
    m_library.current ().name, &ok);
  if (!ok) return;
  if (!m_library.renameSet (m_library.currentIndex (), name)) {
    QMessageBox::warning (this, tr ("Rename macro set"),
                          tr ("That name is empty or already in use."));
    return;
  }
  populateSets ();
}

void JttyMacroEditor::deleteSet ()
{
  if (QMessageBox::question (this, tr ("Delete macro set"),
                             tr ("Delete the macro set \"%1\"?").arg (m_library.current ().name))
      != QMessageBox::Yes) return;
  m_library.removeSet (m_library.currentIndex ());
  populateSets ();
}

void JttyMacroEditor::restoreDefaults ()
{
  if (QMessageBox::question (this, tr ("Restore defaults"),
                             tr ("Replace all macros in \"%1\" with the original ones?")
                             .arg (m_library.current ().name))
      != QMessageBox::Yes) return;
  m_library.restoreDefaults (m_library.currentIndex ());
  showSet (m_library.currentIndex ());
}
