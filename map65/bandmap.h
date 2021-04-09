#ifndef BANDMAP_H
#define BANDMAP_H

#include <QWidget>

namespace Ui {
    class BandMap;
}

class BandMap : public QWidget
{
    Q_OBJECT

public:
  explicit BandMap(QWidget *parent = 0);
  void setText(QString t);
  void setColors(QString t);

  ~BandMap();

protected:
  void resizeEvent(QResizeEvent* event);

private:
    Ui::BandMap *ui;
    QString m_bandMapText;
    QString m_colorBackground;
    QString m_color0;
    QString m_color1;
    QString m_color2;
    QString m_color3;
};

#endif // BANDMAP_H
