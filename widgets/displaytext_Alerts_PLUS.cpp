#include "displaytext.h"

#include <vector>
#include <algorithm>

#include <QAudio>
#include <QAudioOutput>
#include <QSound>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QMouseEvent>
#include <QDateTime>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextBlock>
#include <QMenu>
#include <QAction>
#include <QListIterator>
#include <QRegularExpression>
#include <QScrollBar>

#include "Configuration.hpp"
#include "Decoder/decodedtext.h"
#include "Network/LotWUsers.hpp"
#include "models/DecodeHighlightingModel.hpp"
#include "logbook/logbook.h"
#include "Logger.hpp"

#include "qt_helpers.hpp"
#include "moc_displaytext.cpp"

bool play_CQ = false;
bool play_MyCall = false;
bool play_DXCC = false;
bool play_DXCCOB = false;
bool play_Grid = false;
bool play_GridOB = false;
bool play_Continent = false;
bool play_ContinentOB = false;
bool play_CQZ = false;
bool play_CQZOB = false;
bool play_ITUZ = false;
bool play_ITUZOB = false;

using SpecOp = Configuration::SpecialOperatingActivity;

DisplayText::DisplayText(QWidget *parent)
  : QTextEdit(parent)
  , m_config {nullptr}
  , erase_action_ {new QAction {tr ("&Erase"), this}}
  , high_volume_ {false}
  , modified_vertical_scrollbar_max_ {-1}
{
  setReadOnly (true);
  setUndoRedoEnabled (false);
  viewport ()->setCursor (Qt::ArrowCursor);
  setWordWrapMode (QTextOption::NoWrap);

  // max lines to limit heap usage
  document ()->setMaximumBlockCount (5000);

  // context menu erase action
  setContextMenuPolicy (Qt::CustomContextMenu);
  connect (this, &DisplayText::customContextMenuRequested, [this] (QPoint const& position) {
      auto * menu = createStandardContextMenu (position);
      menu->addAction (erase_action_);
      menu->exec (mapToGlobal (position));
      delete menu;
    });
  connect (erase_action_, &QAction::triggered, this, &DisplayText::erase);
}

void DisplayText::erase ()
{
  clear ();
  Q_EMIT erased ();
}

void DisplayText::setContentFont(QFont const& font)
{
  char_font_ = font;
  selectAll ();
  auto cursor = textCursor ();
  cursor.beginEditBlock ();
  auto char_format = cursor.charFormat ();
  char_format.setFont (char_font_);
  cursor.mergeCharFormat (char_format);
  cursor.clearSelection ();
  cursor.movePosition (QTextCursor::End);

  // position so viewport scrolled to left
  cursor.movePosition (QTextCursor::Up);
  cursor.movePosition (QTextCursor::StartOfLine);
  cursor.endEditBlock ();

  if (!high_volume_ || !m_config || !m_config->decodes_from_top ())
    {
      setTextCursor (cursor);
      ensureCursorVisible ();
    }
}

void DisplayText::mouseDoubleClickEvent(QMouseEvent *e)
{
  Q_EMIT selectCallsign(e->modifiers ());
}

void DisplayText::insertLineSpacer(QString const& line)
{
  insertText (line, "#d3d3d3");
}

namespace
{
  using Highlight = DecodeHighlightingModel::Highlight;
  using highlight_types = std::vector<Highlight>;
  Highlight set_colours (Configuration const * config, QColor * bg, QColor * fg, highlight_types const& types)
  {
    Highlight result = Highlight::CQ;
    if (config)
      {
        QListIterator<DecodeHighlightingModel::HighlightInfo> it {config->decode_highlighting ().items ()};
        // iterate in reverse to honor priorities
        it.toBack ();
        while (it.hasPrevious ())
          {
            auto const& item = it.previous ();
            auto const& type = std::find (types.begin (), types.end (), item.type_);
            if (type != types.end () && item.enabled_)
              {
                if (item.background_.style () != Qt::NoBrush)
                  {
                    *bg = item.background_.color ();
                  }
                if (item.foreground_.style () != Qt::NoBrush)
                  {
                    *fg = item.foreground_.color ();
                  }
                result = item.type_;
              }
          }
      }
    return result;            // highest priority enabled highlighting
  }
}

void DisplayText::insertText(QString const& text, QColor bg, QColor fg
                             , QString const& call1, QString const& call2, QTextCursor::MoveOperation location)
{
  auto cursor = textCursor ();
  cursor.movePosition (location);
  auto block_format = cursor.blockFormat ();
  auto format = cursor.blockCharFormat ();
  format.setFont (char_font_);
  block_format.clearBackground ();
  if (bg.isValid ())
    {
      block_format.setBackground (bg);
    }
  format.clearForeground ();
  if (fg.isValid ())
    {
      format.setForeground (fg);
    }
  if (cursor.position ())
    {
      cursor.insertBlock (block_format, format);
    }
  else
    {
      cursor.setBlockFormat (block_format);
      cursor.setBlockCharFormat (format);
    }

  int text_index {0};
  auto temp_format = format;
  if (call1.size ())
    {
      auto call_index = text.indexOf (call1);
      if (call_index != -1) // sanity check
        {
          auto pos = highlighted_calls_.find (call1);
          if (pos != highlighted_calls_.end ())
            {
              cursor.insertText(text.left (call_index));
              if (pos.value ().first.isValid ())
                {
                  temp_format.setBackground (pos.value ().first);
                }
              if (pos.value ().second.isValid ())
                {
                  temp_format.setForeground (pos.value ().second);
                }
              cursor.insertText(text.mid (call_index, call1.size ()), temp_format);
              text_index = call_index + call1.size ();
            }
        }
    }
  if (call2.size ())
    {
      auto call_index = text.indexOf (call2, text_index);
      if (call_index != -1) // sanity check
        {
          auto pos = highlighted_calls_.find (call2);
          if (pos != highlighted_calls_.end ())
            {
              temp_format = format;
              cursor.insertText(text.mid (text_index, call_index - text_index), format);
              if (pos.value ().second.isValid ())
                {
                  temp_format.setBackground (pos.value ().first);
                }
              if (pos.value ().second.isValid ())
                {
                  temp_format.setForeground (pos.value ().second);
                }
              cursor.insertText(text.mid (call_index, call2.size ()), temp_format);
              text_index = call_index + call2.size ();
            }
        }
    }
  cursor.insertText(text.mid (text_index), format);

  // position so viewport scrolled to left
  cursor.movePosition (QTextCursor::StartOfLine);
  if (!high_volume_ || !m_config || !m_config->decodes_from_top ())
    {
      setTextCursor (cursor);
      ensureCursorVisible ();
    }
  document ()->setMaximumBlockCount (document ()->maximumBlockCount ());
}

void DisplayText::extend_vertical_scrollbar (int min, int max)
{
  static int mod_last;
  static int height;
  if (high_volume_ && m_config && m_config->decodes_from_top ())
    {
      auto m = modified_vertical_scrollbar_max_;
      if (m != mod_last) { height = m - mod_last;mod_last = m; }
      //auto vp_margins2 = viewportMargins ();
      if (height == 0 && m > viewport()->height()) height =  abs( - viewport()->height());
      //LOG_INFO ("scrollbar min=" << min << " max="  << max << " mod=" << modified_vertical_scrollbar_max_ << " height=" << viewport()->height() << " top=" << vp_margins2.top() << " bottom=" << vp_margins2.bottom()) << " height=" << height << " mod_last=" << mod_last;
      if (max > 60000)
        {
          QString tmp = toPlainText();
          while (tmp != NULL && tmp.length() > 100 &&  max > 50000)
          {
            tmp.remove(0, tmp.indexOf("\n")+1);
            max -= height;
          }
          setPlainText(tmp);
        }
      if (max && max != modified_vertical_scrollbar_max_)
        {
          setViewportMargins (0,4,0,0);  // ensure first line is readable
          auto vp_margins = viewportMargins ();
          // add enough to vertical scroll bar range to allow last
          // decode to just scroll of the top of the view port
          max += viewport ()->height () - vp_margins.top () - vp_margins.bottom ();
          modified_vertical_scrollbar_max_ = max;
        }
      verticalScrollBar ()->setRange (min, max);
    }
}

void DisplayText::new_period ()
{
  alertsTimer.stop ();
  disconnect (&alertsTimer, &QTimer::timeout, this, &DisplayText::AudioAlerts);
  if((m_config->alert_Enabled()) && ((m_config->alert_DXCC()) || (m_config->alert_DXCCOB()) || (m_config->alert_Grid()) ||
     (m_config->alert_GridOB()) || (m_config->alert_Continent()) || (m_config->alert_ContinentOB()) || (m_config->alert_CQZ()) ||
     (m_config->alert_CQZOB()) || (m_config->alert_ITUZ()) || (m_config->alert_ITUZOB()) || (m_config->alert_CQ()))) {
      connect (&alertsTimer, &QTimer::timeout, this, &DisplayText::AudioAlerts);
      alertsTimer.setSingleShot (true);
      alertsTimer.start (1000);
  }

  extend_vertical_scrollbar (verticalScrollBar ()->minimum (), verticalScrollBar ()->maximum ());
  if (high_volume_ && m_config && m_config->decodes_from_top () && !vertical_scroll_connection_)
    {
      vertical_scroll_connection_ = connect (verticalScrollBar (), &QScrollBar::rangeChanged
                                             , [this] (int min, int max) {
                                               extend_vertical_scrollbar (min, max );
                                             });
    }
  verticalScrollBar ()->setSliderPosition (verticalScrollBar ()->maximum ());
}

QString DisplayText::appendWorkedB4 (QString message, QString call, QString const& grid,
                                     QColor * bg, QColor * fg, LogBook const& logBook,
                                     QString const& currentBand, QString const& currentMode,
                                     QString extra)
{
  QString countryName;
  bool callB4;
  bool callB4onBand;
  bool countryB4;
  bool countryB4onBand;
  bool gridB4;
  bool gridB4onBand;
  bool continentB4;
  bool continentB4onBand;
  bool CQZoneB4;
  bool CQZoneB4onBand;
  bool ITUZoneB4;
  bool ITUZoneB4onBand;

  if(call.length()==2) {
    int i0=message.indexOf("CQ "+call);
    call=message.mid(i0+6,-1);
    i0=call.indexOf(" ");
    call=call.mid(0,i0);
  }
  if(call.length()<3) return message;
  if(!call.contains(QRegExp("[0-9]|[A-Z]"))) return message;

  auto const& looked_up = logBook.countries ()->lookup (call);
  logBook.match (call, currentMode, grid, looked_up, callB4, countryB4, gridB4, continentB4, CQZoneB4, ITUZoneB4);
  logBook.match (call, currentMode, grid, looked_up, callB4onBand, countryB4onBand, gridB4onBand,
                 continentB4onBand, CQZoneB4onBand, ITUZoneB4onBand, currentBand);
  if(grid=="") {
    gridB4=true;
    gridB4onBand=true;
  }

  if(callB4onBand) m_points=0;

  message = message.trimmed ();

  highlight_types types;
  // no shortcuts here as some types may be disabled
  if (!countryB4) {
    types.push_back (Highlight::DXCC);
    if (m_config->alert_DXCC()) {
        play_DXCC = true;
    }
  }
  if(!countryB4onBand) {
    types.push_back (Highlight::DXCCBand);
    if (m_config->alert_DXCCOB()) {
       play_DXCCOB = true;
    }
  }
  if(!gridB4) {
    types.push_back (Highlight::Grid);
    if (m_config->alert_Grid()) {
        play_Grid = true;
    }
  }
  if(!gridB4onBand) {
    types.push_back (Highlight::GridBand);
    if (m_config->alert_GridOB()) {
        play_GridOB = true;
    }
  }
  if (!callB4) {
    types.push_back (Highlight::Call);
  }
  if(!callB4onBand) {
    types.push_back (Highlight::CallBand);
  }
  if (!continentB4) {
    types.push_back (Highlight::Continent);
    if (m_config->alert_Continent()) {
        play_Continent = true;
    }
  }
  if(!continentB4onBand) {
    types.push_back (Highlight::ContinentBand);
    if (m_config->alert_ContinentOB()) {
        play_ContinentOB = true;
    }
  }
  if (!CQZoneB4) {
    types.push_back (Highlight::CQZone);
    if (m_config->alert_CQZ()) {
        play_CQZ = true;
    }
  }
  if(!CQZoneB4onBand) {
    types.push_back (Highlight::CQZoneBand);
    if (m_config->alert_CQZOB()) {
        play_CQZOB = true;
    }
  }
  if (!ITUZoneB4) {
    types.push_back (Highlight::ITUZone);
    if (m_config->alert_ITUZ()) {
        play_ITUZ = true;
    }
  }
  if(!ITUZoneB4onBand) {
    types.push_back (Highlight::ITUZoneBand);
    if (m_config->alert_ITUZOB()) {
        play_ITUZOB = true;
    }
  }
  if (m_config && m_config->lotw_users ().user (call))
    {
      types.push_back (Highlight::LotW);
    }
  types.push_back (Highlight::CQ);
  auto top_highlight = set_colours (m_config, bg, fg, types);

  switch (top_highlight)
    {
    case Highlight::Continent:
    case Highlight::ContinentBand:
      extra += AD1CCty::continent (looked_up.continent);
      break;
    case Highlight::CQZone:
    case Highlight::CQZoneBand:
      extra += QString {"CQ Zone %1"}.arg (looked_up.CQ_zone);
      break;
    case Highlight::ITUZone:
    case Highlight::ITUZoneBand:
      extra += QString {"ITU Zone %1"}.arg (looked_up.ITU_zone);
      break;
    default:
      if (m_bPrincipalPrefix)
        {
          extra += looked_up.primary_prefix;
        }
      else
        {
          auto countryName = looked_up.entity_name;

          // do some obvious abbreviations
          countryName.replace ("Islands", "Is.");
          countryName.replace ("Island", "Is.");
          countryName.replace ("North ", "N. ");
          countryName.replace ("Northern ", "N. ");
          countryName.replace ("South ", "S. ");
          countryName.replace ("East ", "E. ");
          countryName.replace ("Eastern ", "E. ");
          countryName.replace ("West ", "W. ");
          countryName.replace ("Western ", "W. ");
          countryName.replace ("Central ", "C. ");
          countryName.replace (" and ", " & ");
          countryName.replace ("Republic", "Rep.");
          countryName.replace ("United States of America", "U.S.A.");
          countryName.replace ("United States", "U.S.A.");
          countryName.replace ("Fed. Rep. of ", "");
          countryName.replace ("French ", "Fr.");
          countryName.replace ("Asiatic", "AS");
          countryName.replace ("European", "EU");
          countryName.replace ("African", "AF");

          // assign WAE entities to the correct DXCC when "Include extra WAE entities" is not selected
          if (!(m_config->include_WAE_entities())) {
            countryName.replace ("Bear Is.", "Svalbard");
            countryName.replace ("Shetland Is.", "Scotland");
            countryName.replace ("AF Italy", "Italy");
            countryName.replace ("Sicily", "Italy");
            countryName.replace ("Vienna Intl Ctr", "Austria");
            countryName.replace ("AF Turkey", "Turkey");
            countryName.replace ("EU Turkey", "Turkey");
          }

          extra += countryName;
        }
    }
    m_CQPriority=DecodeHighlightingModel::highlight_name(top_highlight);

    if(((m_points == 00) or (m_points == -1)) and m_bDisplayPoints) return message;
    return leftJustifyAppendage (message, extra);
}

QString DisplayText::leftJustifyAppendage (QString message, QString const& appendage0) const
{
  QString appendage=appendage0;
  if(m_bDisplayPoints and (m_points>0)) {
    appendage=" " + QString::number(m_points);
    if(m_points<10) appendage=" " + appendage;
  }
  if (appendage.size ())
    {
      // allow for seconds
      int padding {message.indexOf (" ") > 4 ? 2 : 0};

      // use a nbsp to save the start of appended text so we can find
      // it again later, align appended data at a fixed column if
      // there is space otherwise let it float to the right
      int space_count;
      space_count = (40 + m_config->align_steps() + padding - message.size ());
      if (space_count > 0) {
        message += QString {space_count, QChar {' '}};
      }
      message += QChar::Nbsp + appendage;
    }
  return message;
}

void DisplayText::displayDecodedText(DecodedText const& decodedText, QString const& myCall,
                                     QString const& mode,
                                     bool displayDXCCEntity, LogBook const& logBook,
                                     QString const& currentBand, bool ppfx, bool bCQonly,
                                     bool haveFSpread, float fSpread, bool bDisplayPoints, int points,QString distance)
{
  m_points=points;
  m_bDisplayPoints=bDisplayPoints;
  m_bPrincipalPrefix=ppfx;
  QColor bg;
  QColor fg;
  bool CQcall = false;
  auto is_73 = decodedText.messageWords().filter (QRegularExpression {"^(73|RR73)$"}).size();
  if (decodedText.string ().contains ("CQ ")) {
          if (m_config->alert_CQ()) {
              play_CQ = true;
          }
  }
  if (decodedText.string ().contains (" CQ ")
      || decodedText.string ().contains (" CQDX ")
      || decodedText.string ().contains (" QRZ ")
      || (is_73 && (m_config->highlight_73 ())))
    {
      CQcall = true;
    }
  else
    {
      if (bCQonly) return;
    }
  auto message = decodedText.string();
  QString dxCall;
  QString dxGrid;
  decodedText.deCallAndGrid (/*out*/ dxCall, dxGrid);
  QRegularExpression grid_regexp {"\\A(?![Rr]{2}73)[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2}){0,1}\\z"};
  if(!dxGrid.contains(grid_regexp)) dxGrid="";
  message = message.left (message.indexOf (QChar::Nbsp)).trimmed (); // strip appended info
  QString extra;
  QString state;    // NJ0A

  if (displayDXCCEntity && dxGrid.length() > 0  && logBook.countries ()->lookup (dxCall).primary_prefix  == "K") {
      //std::cout << dxCall << " -> " << dxGrid <<  " " << logBook.countries ()->lookup (dxCall).primary_prefix <<"\n";
      if (m_config->GridMap()) {
          if (CQcall || is_73 || m_config->GridMapAll()) {
            state = logBook.countries ()->findState(dxGrid);
          }
      }
  }
  //NJ0A

  if (haveFSpread)
    {
      extra += QString {"%1"}.arg (fSpread, 5, 'f', fSpread < 0.95 ? 3 : 2) + QChar {' '};
    }
  auto ap_pos = message.lastIndexOf (QRegularExpression {R"((?:\?\s)?(?:a[0-9]|q[0-9][0-9*]?)$)"});
  if (ap_pos >= 0)
    {
      extra += message.mid (ap_pos) + QChar {' '};
      message = message.left (ap_pos).trimmed ();
    }
  m_CQPriority="";
  if (CQcall || (is_73 && m_config->highlight_73()) || (mode == "FT4" && m_config->highlight_73() && m_config->NCCC_Sprint()
      && (SpecOp::NA_VHF == m_config->special_op_id()) && decodedText.string().contains(" R ")))
    {
      if (displayDXCCEntity)
        {
          // if enabled add the DXCC entity and B4 status to the end of the
          // preformated text line t1
          auto currentMode = mode;
          message = appendWorkedB4 (message, dxCall, dxGrid, &bg, &fg
                                    , logBook, currentBand, currentMode, extra);
        }
      else
        {
          message = leftJustifyAppendage (message, extra);
          highlight_types types {Highlight::CQ};
          if (m_config && m_config->lotw_users ().user (decodedText.CQersCall()))
            {
              types.push_back (Highlight::LotW);
            }
          set_colours (m_config, &bg, &fg, types);
        }
    }
  else
    {
      if (m_config->show_country_names())
        {
          auto const& looked_up = logBook.countries ()->lookup (dxCall);
          auto countryName = looked_up.entity_name;

          if (m_bPrincipalPrefix) {
              extra += looked_up.primary_prefix;
          } else {
              // do some obvious abbreviations
              countryName.replace ("Islands", "Is.");
              countryName.replace ("Island", "Is.");
              countryName.replace ("North ", "N. ");
              countryName.replace ("Northern ", "N. ");
              countryName.replace ("South ", "S. ");
              countryName.replace ("East ", "E. ");
              countryName.replace ("Eastern ", "E. ");
              countryName.replace ("West ", "W. ");
              countryName.replace ("Western ", "W. ");
              countryName.replace ("Central ", "C. ");
              countryName.replace (" and ", " & ");
              countryName.replace ("Republic", "Rep.");
              countryName.replace ("United States of America", "U.S.A.");
              countryName.replace ("United States", "U.S.A.");
              countryName.replace ("Fed. Rep. of ", "");
              countryName.replace ("French ", "Fr.");
              countryName.replace ("Asiatic", "AS");
              countryName.replace ("European", "EU");
              countryName.replace ("African", "AF");

              // assign WAE entities to the correct DXCC when "Include extra WAE entities" is not selected
              if (!(m_config->include_WAE_entities())) {
                countryName.replace ("Bear Is.", "Svalbard");
                countryName.replace ("Shetland Is.", "Scotland");
                countryName.replace ("AF Italy", "Italy");
                countryName.replace ("Sicily", "Italy");
                countryName.replace ("Vienna Intl Ctr", "Austria");
                countryName.replace ("AF Turkey", "Turkey");
                countryName.replace ("EU Turkey", "Turkey");
              }

              extra += countryName;
          }
          message = leftJustifyAppendage(message, extra);
        }
      else
        {
          message = leftJustifyAppendage (message, extra);
        }
    }

  if (myCall.size ())
    {
      QString regexp {"[ <]" + myCall + "[ >]"};
      if (Radio::is_compound_callsign (myCall))
        {
          regexp = "(?:" + regexp + "|[ <]" + Radio::base_callsign (myCall) + "[ >])";
        }
      if ((decodedText.clean_string () + " ").contains (QRegularExpression {regexp}))
        {
        QStringList tw;
        if (mode == "FT8" or mode == "FT4" or mode == "MSK144") {
          tw=decodedText.string().mid(24).split(" ",SkipEmptyParts);
        } else {
          tw=decodedText.string().mid(22).split(" ",SkipEmptyParts);
        }
          if ((tw.size () > 0 && tw[0].contains(myCall)) or decodedText.clean_string().contains("; " + myCall)) {
            highlight_types types {Highlight::MyCall};
            set_colours (m_config, &bg, &fg, types);
            if (m_config->alert_MyCall()) play_MyCall = true;
          } else {
            highlight_types types {Highlight::Tx};
            set_colours (m_config, &bg, &fg, types);
          }
        }
    }

  if (m_config->GridMap() && !m_bDisplayPoints) {
      if (ppfx) {               //NJ0A
          extra = " ";
      } else {
          extra = "      ";
      }
      message = leftJustifyAppendage(message, state);    //NJ0A
  }

  // display distance and azimuth
  if (distance.length() > 0) {
      if (m_config->align()) {
          if (!displayDXCCEntity) {
              message = leftJustifyAppendage (message, "[" + distance + "]");
          } else {
              QString space = " ";
              if (m_bPrincipalPrefix) {
                  if (message.length() < (49 + m_config->align_steps() + m_config->align_steps2())) {
                      message = leftJustifyAppendage ((message + (space.repeated(30))).left(48 + m_config->align_steps() + m_config->align_steps2()), "[" + distance + "]");
                  } else {
                      message = leftJustifyAppendage (message, " [" + distance + "]");
                  }
              } else {
                  if (message.length() < 59 + m_config->align_steps() + m_config->align_steps2()) {
                      message = leftJustifyAppendage ((message + (space.repeated(40))).left(59 + m_config->align_steps() + m_config->align_steps2()), "[" + distance + "]");
                  } else {
                      message = leftJustifyAppendage (message, "[" + distance + "]");
                  }
              }
          }
      } else {
         message = leftJustifyAppendage (message, "[" + distance + "]");
      }
  }

  insertText (message.trimmed (), bg, fg, decodedText.call (), dxCall);
}


void DisplayText::displayTransmittedText(QString text, QString modeTx, qint32 txFreq,
                                         bool bFastMode, double TRperiod)
{
    QString t1=" @  ";
    if(modeTx=="FT4") t1=" +  ";
    if(modeTx=="FT8") t1=" ~  ";
    if(modeTx=="JT4") t1=" $  ";
    if(modeTx=="Q65") t1=" :  ";
    if(modeTx=="JT65") t1=" #  ";
    if(modeTx=="MSK144") t1=" &  ";
    if(modeTx=="FST4") t1=" `  ";
    QString t2;
    t2 = t2.asprintf("%4d",txFreq);
    QString t;
    if(bFastMode or modeTx=="FT8" or modeTx=="FT4" or (TRperiod<60) or
       (modeTx=="Q65" and TRperiod==60)) {
      t = QDateTime::currentDateTimeUtc().toString("hhmmss") + \
        "  Tx      " + t2 + t1 + text;
    } else if(modeTx.mid(0,6)=="FT8fox") {
      t = QDateTime::currentDateTimeUtc().toString("hhmmss") + \
        " Tx" + modeTx.mid(7) + " " + text;
    } else {
      t = QDateTime::currentDateTimeUtc().toString("hhmm") + \
        "  Tx      " + t2 + t1 + text;
    }
    QColor bg;
    QColor fg;
    highlight_types types {Highlight::Tx};
    set_colours (m_config, &bg, &fg, types);
    insertText (t, bg, fg);
}

void DisplayText::displayQSY(QString text)
{
  QString t = QDateTime::currentDateTimeUtc().toString("hhmmss") + "            " + text;
  insertText (t, "hotpink");
}

void DisplayText::displayHoundToBeCalled(QString t, bool bAtTop, QColor bg, QColor fg)
{
  if (bAtTop)  t = t + "\n"; // need a newline when insertion at top
  insertText(t, bg, fg, "", "", bAtTop ? QTextCursor::Start : QTextCursor::End);
}

namespace
{
  void update_selection (QTextCursor& cursor, QColor const& bg, QColor const& fg)
  {
    QTextCharFormat format {cursor.charFormat ()};
    if (bg.isValid ())
      {
        format.setBackground (bg);
      }
    else
      {
        format.clearBackground ();
      }
    if (fg.isValid ())
      {
        format.setForeground (fg);
      }
    else
      {
        format.clearForeground ();
      }
    cursor.mergeCharFormat (format);
  }

  void reset_selection (QTextCursor& cursor)
  {
    // restore previous text format, we rely on the text
    // char format at he start of the selection being the
    // old one which should be the case
    auto c2 = cursor;
    c2.setPosition (c2.selectionStart ());
    cursor.setCharFormat (c2.charFormat ());
  }
}

namespace
{
  QString get_timestamp (QTextCursor& cursor)
  {
    QString timestamp;
    if (cursor.movePosition (QTextCursor::PreviousCharacter)
        && cursor.movePosition (QTextCursor::StartOfLine)
        && cursor.movePosition (QTextCursor::EndOfWord, QTextCursor::KeepAnchor)
        && cursor.hasSelection ())
      {
        timestamp = cursor.selectedText ();
        cursor.movePosition (QTextCursor::StartOfLine);
      }
    return timestamp;
  }
}

void DisplayText::highlight_callsign (QString const& callsign, QColor const& bg,
                                      QColor const& fg, bool last_period_only)
{
  if (!callsign.size ())
    {
      return;
    }
  auto regexp = callsign;
  // allow for hashed callsigns and escape any regexp metacharacters
  QRegularExpression target {QString {"<?"}
                             + regexp.replace (QLatin1Char {'+'}, QLatin1String {"\\+"})
                                 .replace (QLatin1Char {'.'}, QLatin1String {"\\."})
                                 .replace (QLatin1Char {'?'}, QLatin1String {"\\?"})
                             + QString {">?"}
                             , QRegularExpression::DontCaptureOption};
  QTextCharFormat old_format {currentCharFormat ()};
  QTextCursor cursor {document ()};
  if (last_period_only)
    {
      // highlight each instance of the given callsign (word) in the
      // current period
      cursor.movePosition (QTextCursor::End);
      QTextCursor period_start {cursor};
      QTextCursor prior {cursor};
      auto period_timestamp = get_timestamp (period_start);
      while (period_timestamp.size () && period_timestamp == get_timestamp (prior))
        {
          period_start = prior;
        }
      cursor = period_start;
      while (!cursor.isNull ())
        {
          cursor = document ()->find (target, cursor, QTextDocument::FindWholeWords);
          if (!cursor.isNull () && cursor.hasSelection ())
            {
              if (bg.isValid () || fg.isValid ())
                {
                  update_selection (cursor, bg, fg);
                }
              else
                {
                  reset_selection (cursor);
                }
            }
        }
    }
  else
    {
      auto pos = highlighted_calls_.find (callsign);
      if (bg.isValid () || fg.isValid ())
        {
          auto colours = qMakePair (bg, fg);
          if (pos == highlighted_calls_.end ())
            {
              pos = highlighted_calls_.insert (callsign.toUpper (), colours);
            }
          else
            {
              pos.value () = colours; // update colours
            }
          while (!cursor.isNull ())
            {
              cursor = document ()->find (target, cursor, QTextDocument::FindWholeWords);
              if (!cursor.isNull () && cursor.hasSelection ())
                {
                  update_selection (cursor, bg, fg);
                }
            }
        }
      else
        {
          if (pos != highlighted_calls_.end ())
            {
              highlighted_calls_.erase (pos);
            }
          QTextCursor cursor {document ()};
          while (!cursor.isNull ())
            {
              cursor = document ()->find (target, cursor, QTextDocument::FindWholeWords);
              if (!cursor.isNull () && cursor.hasSelection ())
                {
                  reset_selection (cursor);
                }
            }
        }
    }
  setCurrentCharFormat (old_format);
}

void DisplayText::AudioAlerts()
{
#ifdef WIN32
  if(m_config->alert_Enabled()) {
        QAudioOutput info(QAudioDeviceInfo::defaultOutputDevice());
        QString binPath = QCoreApplication::applicationDirPath();
        QAudioFormat format;
        format.setCodec("audio/pcm");
        format.setSampleRate (48000);
        format.setChannelCount (1);
        format.setSampleSize (16);
        format.setSampleType(QAudioFormat::SignedInt);
        QAudioOutput* audio;
        audio = new QAudioOutput(format, this);
        connect(audio, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));
#else
  if(m_config->alert_Enabled()) {
        QAudioOutput device(QAudioDeviceInfo::defaultOutputDevice());
        QString homePath = QDir::homePath();
#endif
        QFile *effect2 = new QFile(this);
        QFile *effect3 = new QFile(this);
        QFile *effect4 = new QFile(this);
        QFile *effect5 = new QFile(this);
        QFile *effect6 = new QFile(this);
        QFile *effect7 = new QFile(this);
        QFile *effect8 = new QFile(this);
        QFile *effect9 = new QFile(this);
        QFile *effect10 = new QFile(this);
        QFile *effect11 = new QFile(this);
        QFile *effect12 = new QFile(this);
        QFile *effect13 = new QFile(this);
#ifdef WIN32
        effect2->setFileName(QString("%1/%2").arg(binPath, "/sounds/MyCall.wav"));
        effect3->setFileName(QString("%1/%2").arg(binPath, "/sounds/DXCC.wav"));
        effect4->setFileName(QString("%1/%2").arg(binPath, "/sounds/DXCCOnBand.wav"));
        effect5->setFileName(QString("%1/%2").arg(binPath, "/sounds/Continent.wav"));
        effect6->setFileName(QString("%1/%2").arg(binPath, "/sounds/ContinentOnBand.wav"));
        effect7->setFileName(QString("%1/%2").arg(binPath, "/sounds/CQZone.wav"));
        effect8->setFileName(QString("%1/%2").arg(binPath, "/sounds/CQZoneOnBand.wav"));
        effect9->setFileName(QString("%1/%2").arg(binPath, "/sounds/ITUZone.wav"));
        effect10->setFileName(QString("%1/%2").arg(binPath, "/sounds/ITUZoneOnBand.wav"));
        effect11->setFileName(QString("%1/%2").arg(binPath, "/sounds/Grid.wav"));
        effect12->setFileName(QString("%1/%2").arg(binPath, "/sounds/GridOnBand.wav"));
        effect13->setFileName(QString("%1/%2").arg(binPath, "/sounds/CQ.wav"));
#else
        effect2->setFileName(QString("%1/%2").arg(homePath, "/sounds/MyCall.wav"));           // for Linux and macOS
        effect3->setFileName(QString("%1/%2").arg(homePath, "/sounds/DXCC.wav"));             // for Linux and macOS
        effect4->setFileName(QString("%1/%2").arg(homePath, "/sounds/DXCCOnBand.wav"));       // for Linux and macOS
        effect5->setFileName(QString("%1/%2").arg(homePath, "/sounds/Continent.wav"));        // for Linux and macOS
        effect6->setFileName(QString("%1/%2").arg(homePath, "/sounds/ContinentOnBand.wav"));  // for Linux and macOS
        effect7->setFileName(QString("%1/%2").arg(homePath, "/sounds/CQZone.wav"));           // for Linux and macOS
        effect8->setFileName(QString("%1/%2").arg(homePath, "/sounds/CQZoneOnBand.wav"));     // for Linux and macOS
        effect9->setFileName(QString("%1/%2").arg(homePath, "/sounds/ITUZone.wav"));          // for Linux and macOS
        effect10->setFileName(QString("%1/%2").arg(homePath, "/sounds/ITUZoneOnBand.wav"));   // for Linux and macOS
        effect11->setFileName(QString("%1/%2").arg(homePath, "/sounds/Grid.wav"));            // for Linux and macOS
        effect12->setFileName(QString("%1/%2").arg(homePath, "/sounds/GridOnBand.wav"));      // for Linux and macOS
        effect13->setFileName(QString("%1/%2").arg(homePath, "/sounds/CQ.wav"));              // for Linux and macOS
#endif
        static int startIndex = 0;
        int nextStartIndex = startIndex +1;
        switch (startIndex) {
        case 0:
            if (play_MyCall) {

#ifdef WIN32
                effect2->open(QIODevice::ReadOnly);
                audio->start(effect2);
#else
                QSound::play(homePath + "/sounds/MyCall.wav");  // for Linux and macOS
#endif

                play_MyCall = false;
                alertsTimer.start (1000);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 1:
            if (play_DXCC) {

#ifdef WIN32
                effect3->open(QIODevice::ReadOnly);
                audio->start(effect3);
#else
                QSound::play(homePath + "/sounds/DXCC.wav");  // for Linux and macOS
#endif

                play_DXCC = false;
                play_DXCCOB = false;
                alertsTimer.start (1200);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 2:
            if (play_DXCCOB && !play_DXCC) {
#ifdef WIN32
                effect4->open(QIODevice::ReadOnly);
                audio->start(effect4);
#else
                QSound::play(homePath + "/sounds/DXCCOnBand.wav");  // for Linux and macOS
#endif

                play_DXCCOB = false;
                alertsTimer.start (1800);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 3:
            if (play_Continent) {

#ifdef WIN32
                effect5->open(QIODevice::ReadOnly);
                audio->start(effect5);
#else
                QSound::play(homePath + "/sounds/Continent.wav");  // for Linux and macOS
#endif
                play_Continent = false;
                play_ContinentOB = false;
                play_GridOB = false;
                play_CQZOB = false;
                play_ITUZOB = false;
                alertsTimer.start (1000);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 4:
            if (play_ContinentOB && !play_Continent) {
#ifdef WIN32
                effect6->open(QIODevice::ReadOnly);
                audio->start(effect6);
#else
                QSound::play(homePath + "/sounds/ContinentOnBand.wav");  // for Linux and macOS
#endif
                play_ContinentOB = false;
                play_GridOB = false;
                play_CQZOB = false;
                play_ITUZOB = false;
                alertsTimer.start (2000);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 5:
            if (play_CQZ) {
#ifdef WIN32
                effect7->open(QIODevice::ReadOnly);
                audio->start(effect7);
#else
                QSound::play(homePath + "/sounds/CQZone.wav");  // for Linux and macOS
#endif
                play_CQZ = false;
                play_CQZOB = false;
                alertsTimer.start (1500);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 6:
            if (play_CQZOB && !play_CQZ) {
#ifdef WIN32
                effect8->open(QIODevice::ReadOnly);
                audio->start(effect8);
#else
                QSound::play(homePath + "/sounds/CQZoneOnBand.wav");  // for Linux and macOS
#endif
                play_CQZOB = false;
                alertsTimer.start (1800);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 7:
            if (play_ITUZ) {
#ifdef WIN32
                effect9->open(QIODevice::ReadOnly);
                audio->start(effect9);
#else
                QSound::play(homePath + "/sounds/ITUZone.wav");  // for Linux and macOS
#endif
                play_ITUZ = false;
                play_ITUZOB = false;
                play_GridOB = false;
                alertsTimer.start (1500);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 8:
            if (play_ITUZOB && !(play_ITUZ)) {
#ifdef WIN32
                effect10->open(QIODevice::ReadOnly);
                audio->start(effect10);
#else
                QSound::play(homePath + "/sounds/ITUZoneOnBand.wav");  // for Linux and macOS
#endif
                play_ITUZOB = false;
                play_GridOB = false;
                alertsTimer.start (1900);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 9:
            if (play_Grid) {
#ifdef WIN32
                effect11->open(QIODevice::ReadOnly);
                audio->start(effect11);
#else
                QSound::play(homePath + "/sounds/Grid.wav");  // for Linux and macOS
#endif
                play_Grid = false;
                play_GridOB = false;
                alertsTimer.start (1000);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 10:
            if (play_GridOB && !play_Grid) {
#ifdef WIN32
                effect12->open(QIODevice::ReadOnly);
                audio->start(effect12);
#else
                QSound::play(homePath + "/sounds/GridOnBand.wav");  // for Linux and macOS
#endif
                play_GridOB = false;
                alertsTimer.start (1500);
                startIndex = nextStartIndex;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 11:
            if (play_CQ) {
#ifdef WIN32
                effect13->open(QIODevice::ReadOnly);
                audio->start(effect13);
#else
                QSound::play(homePath + "/sounds/CQ.wav");  // for Linux and macOS
#endif
                play_CQ = false;
                alertsTimer.start (1000);
                nextStartIndex++;
                return;
            } else {
                nextStartIndex++;
            }
            Q_FALLTHROUGH();
        case 12:
            // stop any running alerts timer, clear temp data, and restart alerts timer
            alertsTimer.stop ();
#ifdef WIN32
            effect2->close();
            effect2->deleteLater();
            effect3->close();
            effect3->deleteLater();
            effect4->close();
            effect4->deleteLater();
            effect5->close();
            effect5->deleteLater();
            effect6->close();
            effect6->deleteLater();
            effect7->close();
            effect7->deleteLater();
            effect8->close();
            effect8->deleteLater();
            effect9->close();
            effect9->deleteLater();
            effect10->close();
            effect10->deleteLater();
            effect11->close();
            effect11->deleteLater();
            effect12->close();
            effect12->deleteLater();
            effect13->close();
            effect13->deleteLater();
            audio->deleteLater();  // remove QAudioSink to avoid a memory leak
#endif
            alertsTimer.start (1250);
            startIndex = 0;
            return;
        }
  }
}
