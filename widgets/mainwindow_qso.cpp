#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <cmath>

QsoLogData MainWindow::captureLogQso ()
{
  if (!m_dateTimeQSOOn.isValid ()) {
    auto const now = QDateTime::currentDateTimeUtc ();
    m_dateTimeQSOOn = now.addSecs (-(m_ntx - 2) * int (m_TRperiod)
      - int (std::fmod (double (now.time ().second ()), m_TRperiod)));
  }
  auto grid = m_hisGrid == "...." ? QString {} : m_hisGrid;
  if (m_config.ZZ00 () && m_hisGrid.isEmpty () && m_specOp != SpecOp::NONE
      && m_specOp != SpecOp::FOX && m_specOp != SpecOp::HOUND) m_hisGrid = "ZZ00";
  switch (m_specOp) {
  case SpecOp::NA_VHF:
  case SpecOp::WW_DIGI:
  case SpecOp::ARRL_DIGI:
    m_xSent = m_config.my_grid ().left (4);
    m_xRcvd = m_hisGrid.left (4);
    break;
  case SpecOp::EU_VHF: {
    m_rptSent = m_xSent.section (' ', 0, 0).left (2);
    m_rptRcvd = m_xRcvd.section (' ', 0, 0).left (2);
    auto const fields = m_xRcvd.split (' ', SkipEmptyParts);
    if (fields.size () >= 2) m_hisGrid = fields.at (1);
    grid = m_hisGrid;
    ui->dxGridEntry->setText (grid);
    break;
  }
  case SpecOp::FIELD_DAY:
  case SpecOp::RTTY:
    m_rptSent = m_xSent.section (' ', 0, 0);
    m_rptRcvd = m_xRcvd.section (' ', 0, 0);
    break;
  case SpecOp::Q65_PILEUP:
    m_xSent = m_config.my_grid ().left (4);
    m_xRcvd = m_hisGrid;
    break;
  default: break;
  }
  QsoLogData data;
  data.call = m_hisCall;
  data.grid = grid;
  data.mode = m_mode;
  data.rptSent = m_rptSent;
  data.rptRcvd = m_rptRcvd;
  data.dateTimeOn = m_dateTimeQSOOn;
  data.dateTimeOff = qMax (data.dateTimeOn, QDateTime::currentDateTimeUtc ());
  data.dialFreq = m_operatingFrequency.rx () + ui->TxFreqSpinBox->value ();
  data.noSuffix = m_noSuffix;
  data.xSent = m_xSent;
  data.xRcvd = m_xRcvd;
  data.specOp = m_specOp;
  data.myCall = m_config.my_callsign ();
  data.myGrid = m_config.my_grid ();
  data.operatorCall = m_config.opCall ();
  data.contestName = m_config.Contest_Name ();
  data.individualContestName = m_config.Individual_Contest_Name ();
  data.superFox = m_config.superFox ();
  return data;
}

