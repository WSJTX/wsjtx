#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Configuration.hpp"
#include "models/FrequencyList.hpp"
#include <QTimer>

extern bool m_displayBand;
extern bool keep_frequency;

void MainWindow::bandHoppingTimer()
{
    if(ui->pbBandHopping->isChecked()) {
    static int startIndex = 0;
    int nextStartIndex = startIndex +1;
    switch (startIndex){
    case 0:
            startIndex = nextStartIndex;  // band hopping every other minute
            return;
    case 1:
            m_displayBand = false;
            bandHopping();
            startIndex = 0;
            return;
     }
   }
}

void MainWindow::bandHopping()
{
    if(ui->pbBandHopping->isChecked() && !ui->autoButton->isChecked() && !ui->tuneButton->isChecked()) {
    static int startIndex = 0;
    int nextStartIndex = startIndex +1;
    switch (startIndex){
    case 0:
        if (ui->cb160m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (1840000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 1:
        if (ui->cb80m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (3573000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 2:
        if (ui->cb60m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (5357000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 3:
        if (ui->cb40m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (7074000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 4:
        if (ui->cb30m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (10136000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 5:
        if (ui->cb20m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (14074000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 6:
        if (ui->cb17m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (18100000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 7:
        if (ui->cb15m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (21074000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 8:
        if (ui->cb12m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (24915000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 9:
        if (ui->cb10m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (28074000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 10:
        if (ui->cb6m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (50313000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 11:
        if (ui->cb4m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (70154000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 12:
        if (ui->cb2m->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (144174000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 13:
        if (ui->cb70cm->isChecked()) {
            on_actionFT8_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (432174000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 14:
       if (ui->cb80mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (3575000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 15:
        if (ui->cb40mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (7047500);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 16:
        if (ui->cb30mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (10140000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 17:
        if (ui->cb20mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (14080000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 18:
        if (ui->cb17mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (18104000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();

    case 19:
        if (ui->cb15mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (21140000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 20:
        if (ui->cb12mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (24919000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 21:
        if (ui->cb10mFT4->isChecked()) {
            on_actionFT4_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (28180000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 22:
        if (ui->cb2mMSK->isChecked()) {
            on_actionMSK144_triggered();
            auto const& row = m_config.frequencies ()->best_working_frequency (144360000);
            ui->bandComboBox->setCurrentIndex (row);
            if (row >= 0) on_bandComboBox_activated (row);
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 23:
        if (ui->cbQRG1->isChecked()) {
            int f1 = ui->sbQRG1->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f1);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 24:
        if (ui->cbQRG2->isChecked()) {
            int f2 = ui->sbQRG2->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f2);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 25:
        if (ui->cbQRG3->isChecked()) {
            int f3 = ui->sbQRG3->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f3);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 26:
        if (ui->cbQRG4->isChecked()) {
            int f4 = ui->sbQRG4->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f4);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 27:
        if (ui->cbQRG5->isChecked()) {
            int f5 = ui->sbQRG5->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f5);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 28:
        if (ui->cbQRG6->isChecked()) {
            int f6 = ui->sbQRG6->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f6);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 29:
        if (ui->cbQRG7->isChecked()) {
            int f7 = ui->sbQRG7->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f7);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = nextStartIndex;
            return;
        } else {
            nextStartIndex++;
        }
        Q_FALLTHROUGH();
    case 30:
        if (ui->cbQRG8->isChecked()) {
            int f8 = ui->sbQRG8->value()*1000;
            keep_frequency = true;
            QTimer::singleShot (250, [=] {keep_frequency = false;});
            setRig (f8);
            on_actionFT8_triggered();
            ui->pbBandHopping->setChecked(true);
            startIndex = 0;
            return;
            bandHopping();
        } else {
            startIndex = 0;
            bandHopping();
        }
    }
  }
}
