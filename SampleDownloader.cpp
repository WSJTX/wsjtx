#include "SampleDownloader.hpp"

#include <QString>
#include <QSettings>
#include <QtWidgets>

#include "pimpl_impl.hpp"
#include "SettingsGroup.hpp"
#include "SampleDownloader/Directory.hpp"
#include "widgets/MessageBox.hpp"

#include "moc_SampleDownloader.cpp"

namespace
{
  char const * const title = "Download Samples";
}

class SampleDownloader::impl final
  : public QDialog
{
  Q_OBJECT

public:
  explicit impl (QSettings * settings, Configuration const *, QNetworkAccessManager *, QWidget * parent);
  ~impl () {save_window_state ();}

  void refresh ()
  {
    show ();
    raise ();
    activateWindow ();
    directory_.refresh (http_only_check_box_.isChecked ());
  }

protected:
  void closeEvent (QCloseEvent * e) override
  {
    save_window_state ();
    QDialog::closeEvent (e);
  }

private:
  void save_window_state ()
  {
    SettingsGroup g (settings_, title);
    settings_->setValue ("geometry", saveGeometry ());
    settings_->setValue ("SamplesURL", url_line_edit_.text ());
    settings_->setValue ("HTTPOnly", http_only_check_box_.isChecked ());
  }

  Q_SLOT void button_clicked (QAbstractButton *);

  QSettings * settings_;
  Directory directory_;
  QGridLayout main_layout_;
  QVBoxLayout left_layout_;
  QDialogButtonBox button_box_;
  QWidget details_widget_;
  QFormLayout details_layout_;
  QLineEdit url_line_edit_;
  QCheckBox http_only_check_box_;
};

#include "SampleDownloader.moc"

SampleDownloader::SampleDownloader (QSettings * settings, Configuration const * configuration
                                    , QNetworkAccessManager * network_manager, QWidget * parent)
  : m_ {settings, configuration, network_manager, parent}
{
}

SampleDownloader::~SampleDownloader ()
{
}

void SampleDownloader::show ()
{
  m_->refresh ();
}

SampleDownloader::impl::impl (QSettings * settings
                              , Configuration const * configuration
                              , QNetworkAccessManager * network_manager
                              , QWidget * parent)
  : QDialog {parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint}
  , settings_ {settings}
  , directory_ {configuration, network_manager}
  , button_box_ {QDialogButtonBox::Close, Qt::Vertical}
{
  setWindowTitle (windowTitle () + ' ' + tr ("Download Samples"));
  resize (500, 600);
  {
    SettingsGroup g {settings_, title};
    restoreGeometry (settings_->value ("geometry", saveGeometry ()).toByteArray ());
    url_line_edit_.setText (settings_->value ("SamplesURL", PROJECT_SAMPLES_URL).toString ());
    http_only_check_box_.setChecked (settings_->value ("HTTPOnly", false).toBool ());
    directory_.url_root (url_line_edit_.text ());
  }

  setWindowTitle (QApplication::applicationName () + " - " + tr ("Download Samples"));

  button_box_.button (QDialogButtonBox::Close)->setDefault (true);
  button_box_.addButton (tr ("&Abort"), QDialogButtonBox::DestructiveRole);
  button_box_.addButton (tr ("&Refresh"), QDialogButtonBox::ResetRole);
  left_layout_.addWidget (&directory_);

  auto details_button = button_box_.addButton (tr ("&Details"), QDialogButtonBox::HelpRole);
  details_button->setCheckable (true);
  details_widget_.hide ();
  details_layout_.setMargin (0);
  details_layout_.addRow (tr ("Base URL for samples:"), &url_line_edit_);
  details_layout_.addRow (tr ("Only use HTTP:"), &http_only_check_box_);
  http_only_check_box_.setToolTip (tr ("Check this if you get SSL/TLS errors"));
  details_widget_.setLayout (&details_layout_);

  main_layout_.addLayout (&left_layout_, 0, 0);
  main_layout_.addWidget (&button_box_, 0, 1);
  main_layout_.addWidget (&details_widget_, 1, 0, 1, 2);
  main_layout_.setRowStretch (0, 2);
  setLayout (&main_layout_);

  connect (&button_box_, &QDialogButtonBox::clicked, this, &SampleDownloader::impl::button_clicked);
  connect (details_button, &QAbstractButton::clicked, &details_widget_, &QWidget::setVisible);
  connect (&url_line_edit_, &QLineEdit::editingFinished, [this] () {
      if (directory_.url_root (url_line_edit_.text ()))
        {
          directory_.refresh (http_only_check_box_.isChecked ());
        }
      else
        {
          MessageBox::warning_message (this, tr ("Input Error"), tr ("Invalid URL format"));
        }
    });
  connect (&http_only_check_box_, &QAbstractButton::toggled, [this] (bool checked) {
      directory_.refresh (checked);
    });
}

void SampleDownloader::impl::button_clicked (QAbstractButton * button)
{
  switch (button_box_.buttonRole (button))
    {
    case QDialogButtonBox::RejectRole:
      hide ();
      break;

    case QDialogButtonBox::DestructiveRole:
      directory_.abort ();
      break;

    case QDialogButtonBox::ResetRole:
      directory_.refresh (http_only_check_box_.isChecked ());
      break;

    default:
      break;
    }
}
