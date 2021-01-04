#include "colorhighlighting.h"

#include <QApplication>
#include <QString>

#include "SettingsGroup.hpp"
#include "models/DecodeHighlightingModel.hpp"

#include "ui_colorhighlighting.h"
#include "moc_colorhighlighting.cpp"

ColorHighlighting::ColorHighlighting (QSettings * settings, DecodeHighlightingModel const& highlight_model, QWidget * parent)
  : QDialog {parent}
  , ui {new Ui::ColorHighlighting}
  , settings_ {settings}
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - Colors");
  read_settings ();
  set_items (highlight_model);
}

ColorHighlighting::~ColorHighlighting()
{
  if (isVisible ()) write_settings ();
}

void ColorHighlighting::read_settings ()
{
  SettingsGroup group {settings_, "ColorScheme"};
  restoreGeometry (settings_->value ("window/geometry").toByteArray ());
}

void ColorHighlighting::write_settings ()
{
  SettingsGroup group {settings_, "ColorScheme"};
  settings_->setValue ("window/geometry", saveGeometry ());
}

void ColorHighlighting::set_items (DecodeHighlightingModel const& highlighting_model)
{
  int index {0};
  for (auto const& item : highlighting_model.items ())
    {
      QLabel * example;
      QLabel * label;
      switch (index++)
        {
        case 0:
          example = ui->example1_label;
          label = ui->p1_label;
          break;
        case 1:
          example = ui->example2_label;
          label = ui->p2_label;
          break;
        case 2:
          example = ui->example3_label;
          label = ui->p3_label;
          break;
        case 3:
          example = ui->example4_label;
          label = ui->p4_label;
          break;
        case 4:
          example = ui->example5_label;
          label = ui->p5_label;
          break;
        case 5:
          example = ui->example6_label;
          label = ui->p6_label;
          break;
        case 6:
          example = ui->example7_label;
          label = ui->p7_label;
          break;
        case 7:
          example = ui->example8_label;
          label = ui->p8_label;
          break;
        case 8:
          example = ui->example9_label;
          label = ui->p9_label;
          break;
        case 9:
          example = ui->example10_label;
          label = ui->p10_label;
          break;
        case 10:
          example = ui->example11_label;
          label = ui->p11_label;
          break;
        case 11:
          example = ui->example12_label;
          label = ui->p12_label;
          break;
        case 12:
          example = ui->example13_label;
          label = ui->p13_label;
          break;
        case 13:
          example = ui->example14_label;
          label = ui->p14_label;
          break;
        case 14:
          example = ui->example15_label;
          label = ui->p15_label;
          break;
        case 15:
          example = ui->example16_label;
          label = ui->p16_label;
          break;
        default:
          continue;
        }
      auto style_sheet = example->parentWidget ()->styleSheet ();
      if (Qt::NoBrush != item.background_.style ())
        {
          style_sheet += QString {"; background-color: #%1"}.arg (item.background_.color ().rgb (), 8, 16, QLatin1Char {'0'});
        }
      if (Qt::NoBrush != item.foreground_.style ())
        {
          style_sheet += QString {"; color: #%1"}.arg (item.foreground_.color ().rgb (), 8, 16, QLatin1Char {'0'});
        }
      example->setStyleSheet (style_sheet);
      example->setEnabled (item.enabled_);
      label->setText (DecodeHighlightingModel::highlight_name (item.type_));
      label->setEnabled (item.enabled_);
    }
}
