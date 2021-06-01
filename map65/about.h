#ifndef ABOUTDLG_H
#define ABOUTDLG_H

#include <QDialog>

namespace Ui {
    class CAboutDlg;
}

class CAboutDlg : public QDialog
{
    Q_OBJECT

public:
	explicit CAboutDlg(QWidget *parent = nullptr);
  ~CAboutDlg();

private:
	Ui::CAboutDlg *ui;
	QString m_Str;
};

#endif // ABOUTDLG_H
