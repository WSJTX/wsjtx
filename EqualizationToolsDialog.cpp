#include "EqualizationToolsDialog.hpp"

#include <iterator>
#include <algorithm>
#include <fstream>
#include <limits>
#include <cmath>

#include <QDir>
#include <QVector>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QSettings>

#include "SettingsGroup.hpp"
#include "qcustomplot.h"
#include "pimpl_impl.hpp"

namespace
{
  float constexpr PI = 3.1415927f;
  char const * const title = "Equalization Tools";
  size_t constexpr intervals = 144;

  // plot data loaders - wraps a plot providing value_type and
  // push_back so that a std::back_inserter output iterator can be
  // used to load plot data
  template<typename T, typename A>
  struct plot_data_loader
  {
  public:
    typedef T value_type;

    // the adjust argument is a function that is passed the plot
    // pointer, the graph index and a data point, it returns a
    // possibly adjusted data point and can modify the graph including
    // adding extra points or gaps (quiet_NaN)
    plot_data_loader (QCustomPlot * plot, int graph_index, A adjust)
      : plot_ {plot}
      , index_ {graph_index}
      , adjust_ (adjust)
    {
    }

    // load point into graph
    void push_back (value_type const& d)
    {
      plot_->graph (index_)->data ()->add (adjust_ (plot_, index_, d));
    }

  private:
    QCustomPlot * plot_;
    int index_;
    A adjust_;
  };
  // helper function template to make a plot_data_loader instance
  template<typename A>
  auto make_plot_data_loader (QCustomPlot * plot, int index, A adjust)
    -> plot_data_loader<QCPGraphData, decltype (adjust)>
  {
    return plot_data_loader<QCPGraphData, decltype (adjust)> {plot, index, adjust};
  }
  // identity adjust function when no adjustment is needed with the
  // above instantiation helper
  QCPGraphData adjust_identity (QCustomPlot *, int, QCPGraphData const& v) {return v;}

  // a plot_data_loader adjustment function that wraps Y values of
  // (-1..+1) plotting discontinuities as gaps in the graph data
  auto wrap_pi = [] (QCustomPlot * plot, int index, QCPGraphData d) 
  {
    double constexpr limit {1};
    static unsigned wrap_count {0};
    static double last_x {std::numeric_limits<double>::lowest ()};

    d.value += 2 * limit * wrap_count;
    if (d.value > limit)
      {
        // insert a gap in the graph
        plot->graph (index)->data ()->add ({last_x + (d.key - last_x) / 2
              , std::numeric_limits<double>::quiet_NaN ()});
        while (d.value > limit)
          {
            --wrap_count;
            d.value -= 2 * limit;
          }
      }
    else if (d.value < -limit)
      {
        // insert a gap into the graph
        plot->graph (index)->data ()->add ({last_x + (d.key - last_x) / 2
              , std::numeric_limits<double>::quiet_NaN ()});
        while (d.value < -limit)
          {
            ++wrap_count;
            d.value += 2 * limit;
          }
      }
    last_x = d.key;
    return d;
  };

  // generate points of type R from a function of type F for X in
  // (-1..+1) with N intervals and function of type SX to scale X and
  // of type SY to scale Y
  //
  // it is up to the user to call the generator sufficient times which
  // is interval+1 times to reach +1
  template<typename R, typename F, typename SX, typename SY>
  struct graph_generator
  {
  public:
    graph_generator (F f, size_t intervals, SX x_scaling, SY y_scaling)
      : x_ {0}
      , f_ (f)
      , intervals_ {intervals}
      , x_scaling_ (x_scaling)
      , y_scaling_ (y_scaling)
    {
    }

    R operator () ()
    {
      typename F::value_type x {x_++ * 2.f / intervals_ - 1.f};
      return {x_scaling_ (x), y_scaling_ (f_ (x))};
    }

  private:
    int x_;
    F f_;
    size_t intervals_;
    SX x_scaling_;
    SY y_scaling_;
  };
  // helper function template to make a graph_generator instance for
  // QCPGraphData type points with intervals intervals
  template<typename F, typename SX, typename SY>
  auto make_graph_generator (F function, SX x_scaling, SY y_scaling)
    -> graph_generator<QCPGraphData, F, decltype (x_scaling), decltype (y_scaling)>
  {
    return graph_generator<QCPGraphData, F, decltype (x_scaling), decltype (y_scaling)>
      {function, intervals, x_scaling, y_scaling};
  }

  // template function object for a polynomial with coefficients
  template<typename C>
  class polynomial
  {
  public:
    typedef typename C::value_type value_type;

    explicit polynomial (C const& coefficients)
      : c_ {coefficients}
    {
    }

    value_type operator () (value_type const& x)
    {
      value_type y {};
      for (typename C::size_type i = c_.size (); i > 0; --i)
        {
          y = c_[i - 1] + x * y;
        }
      return y;
    }

  private:
    C c_;
  };
  // helper function template to instantiate a polynomial instance
  template<typename C>
  auto make_polynomial (C const& coefficients) -> polynomial<C>
  {
    return polynomial<C> (coefficients);
  }

  // template function object for a group delay with coefficients
  template<typename C>
  class group_delay
  {
  public:
    typedef typename C::value_type value_type;

    explicit group_delay (C const& coefficients)
      : c_ {coefficients}
    {
    }

    value_type operator () (value_type const& x)
    {
      value_type tau {};
      for (typename C::size_type i = 2; i < c_.size (); ++i)
        {
          tau += i * c_[i] * std::pow (x, i - 1);
        }
      return -1 / (2 * PI) * tau;
    }

  private:
    C c_;
  };
  // helper function template to instantiate a group_delay function
  // object
  template<typename C>
  auto make_group_delay (C const& coefficients) -> group_delay<C>
  {
    return group_delay<C> (coefficients);
  }

  // handy identity function
  template<typename T> T identity (T const& v) {return v;}

  // a lambda that scales the X axis from normalized to (500..2500)Hz
  auto freq_scaling = [] (float v) -> float {return 1500.f + 1000.f * v;};

  // a lambda that scales the phase Y axis from radians to units of Pi
  auto pi_scaling = [] (float v) -> float {return v / PI;};
}

class EqualizationToolsDialog::impl final
  : public QDialog
{
  Q_OBJECT

public:
  explicit impl (EqualizationToolsDialog * self, QSettings * settings
                 , QDir const& data_directory, QVector<double> const& coefficients
                 , QWidget * parent);
  ~impl () {save_window_state ();}

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
  }

  void plot_current ();
  void plot_phase ();
  void plot_amplitude ();

  EqualizationToolsDialog * self_;
  QSettings * settings_;
  QDir data_directory_;
  QHBoxLayout layout_;
  QVector<double> current_coefficients_;
  QVector<double> new_coefficients_;
  unsigned amp_poly_low_;
  unsigned amp_poly_high_;
  QVector<double> amp_coefficients_;
  QCustomPlot plot_;
  QDialogButtonBox button_box_;
};

#include "EqualizationToolsDialog.moc"

EqualizationToolsDialog::EqualizationToolsDialog (QSettings * settings
                                                  , QDir const& data_directory
                                                  , QVector<double> const& coefficients
                                                  , QWidget * parent)
  : m_ {this, settings, data_directory, coefficients, parent}
{
}

void EqualizationToolsDialog::show ()
{
  m_->show ();
}

EqualizationToolsDialog::impl::impl (EqualizationToolsDialog * self
                                     , QSettings * settings
                                     , QDir const& data_directory
                                     , QVector<double> const& coefficients
                                     , QWidget * parent)
  : QDialog {parent}
  , self_ {self}
  , settings_ {settings}
  , data_directory_ {data_directory}
  , current_coefficients_ {coefficients}
  , amp_poly_low_ {0}
  , amp_poly_high_ {6000}
  , button_box_ {QDialogButtonBox::Apply
        | QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Close
        , Qt::Vertical}
{
  setWindowTitle (windowTitle () + ' ' + tr ("Equalization Tools"));
  resize (500, 600);
  {
    SettingsGroup g {settings_, title};
    restoreGeometry (settings_->value ("geometry", saveGeometry ()).toByteArray ());
  }

  auto legend_title = new QCPTextElement {&plot_, tr ("Phase"), QFont {"sans", 9, QFont::Bold}};
  legend_title->setLayer (plot_.legend->layer ());
  plot_.legend->addElement (0, 0, legend_title);
  plot_.legend->setVisible (true);

  plot_.xAxis->setLabel (tr ("Freq (Hz)"));
  plot_.xAxis->setRange (500, 2500);
  plot_.yAxis->setLabel (tr ("Phase (Π)"));
  plot_.yAxis->setRange (-1, +1);
  plot_.yAxis2->setLabel (tr ("Delay (ms)"));
  plot_.axisRect ()->setRangeDrag (Qt::Vertical);
  plot_.axisRect ()->setRangeZoom (Qt::Vertical);
  plot_.yAxis2->setVisible (true);
  plot_.axisRect ()->setRangeDragAxes (nullptr, plot_.yAxis2);
  plot_.axisRect ()->setRangeZoomAxes (nullptr, plot_.yAxis2);
  plot_.axisRect ()->insetLayout ()->setInsetAlignment (0, Qt::AlignBottom|Qt::AlignRight);
  plot_.setInteractions (QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);

  plot_.addGraph ()->setName (tr ("Measured"));
  plot_.graph ()->setPen (QPen {Qt::blue});
  plot_.graph ()->setVisible (false);
  plot_.graph ()->removeFromLegend ();

  plot_.addGraph ()->setName (tr ("Proposed"));
  plot_.graph ()->setPen (QPen {Qt::red});
  plot_.graph ()->setVisible (false);
  plot_.graph ()->removeFromLegend ();

  plot_.addGraph ()->setName (tr ("Current"));
  plot_.graph ()->setPen (QPen {Qt::green});

  plot_.addGraph (plot_.xAxis, plot_.yAxis2)->setName (tr ("Group Delay"));
  plot_.graph ()->setPen (QPen {Qt::darkGreen});

  plot_.plotLayout ()->addElement (new QCPAxisRect {&plot_});
  plot_.plotLayout ()->setRowStretchFactor (1, 0.5);

  auto amp_legend = new QCPLegend;
  plot_.axisRect (1)->insetLayout ()->addElement (amp_legend, Qt::AlignTop | Qt::AlignRight);
  plot_.axisRect (1)->insetLayout ()->setMargins (QMargins {12, 12, 12, 12});
  amp_legend->setVisible (true);
  amp_legend->setLayer (QLatin1String {"legend"});
  legend_title = new QCPTextElement {&plot_, tr ("Amplitude"), QFont {"sans", 9, QFont::Bold}};
  legend_title->setLayer (amp_legend->layer ());
  amp_legend->addElement (0, 0, legend_title);

  plot_.axisRect (1)->axis (QCPAxis::atBottom)->setLabel (tr ("Freq (Hz)"));
  plot_.axisRect (1)->axis (QCPAxis::atBottom)->setRange (0, 6000);
  plot_.axisRect (1)->axis (QCPAxis::atLeft)->setLabel (tr ("Relative Power (dB)"));
  plot_.axisRect (1)->axis (QCPAxis::atLeft)->setRangeLower (0);
  plot_.axisRect (1)->setRangeDragAxes (nullptr, nullptr);
  plot_.axisRect (1)->setRangeZoomAxes (nullptr, nullptr);

  plot_.addGraph (plot_.axisRect (1)->axis (QCPAxis::atBottom)
                  , plot_.axisRect (1)->axis (QCPAxis::atLeft))->setName (tr ("Reference"));
  plot_.graph ()->setPen (QPen {Qt::blue});
  plot_.graph ()->removeFromLegend ();
  plot_.graph ()->addToLegend (amp_legend);

  layout_.addWidget (&plot_);

  auto load_phase_button = button_box_.addButton (tr ("Phase ..."), QDialogButtonBox::ActionRole);
  auto refresh_button = button_box_.addButton (tr ("Refresh"), QDialogButtonBox::ActionRole);
  auto discard_measured_button = button_box_.addButton (tr ("Discard Measured"), QDialogButtonBox::ActionRole);
  layout_.addWidget (&button_box_);
  setLayout (&layout_);

  connect (&button_box_, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect (&button_box_, &QDialogButtonBox::clicked, [=] (QAbstractButton * button) {
      if (button == load_phase_button)
        {
          plot_phase ();
        }
      else if (button == refresh_button)
        {
          plot_current ();
        }
      else if (button == button_box_.button (QDialogButtonBox::Apply))
        {
          if (plot_.graph (0)->dataCount ()) // something loaded
            {
              current_coefficients_ = new_coefficients_;
              Q_EMIT self_->phase_equalization_changed (current_coefficients_);
              plot_current ();
            }
        }
      else if (button == button_box_.button (QDialogButtonBox::RestoreDefaults))
        {
          current_coefficients_ = QVector<double> {0., 0., 0., 0., 0.};
          Q_EMIT self_->phase_equalization_changed (current_coefficients_);
          plot_current ();
        }
      else if (button == discard_measured_button)
        {
          new_coefficients_ = QVector<double> {0., 0., 0., 0., 0.};

          plot_.graph (0)->data ()->clear ();
          plot_.graph (0)->setVisible (false);
          plot_.graph (0)->removeFromLegend ();

          plot_.graph (1)->data ()->clear ();
          plot_.graph (1)->setVisible (false);
          plot_.graph (1)->removeFromLegend ();

          plot_.replot ();
        }
    });

  plot_current ();
}

struct PowerSpectrumPoint
{
  operator QCPGraphData () const
  {
    return QCPGraphData {freq_, power_};
  }

  float freq_;
  float power_;
};

// read an amplitude point line from a stream (refspec.dat)
std::istream& operator >> (std::istream& is, PowerSpectrumPoint& r)
{
  float y1, y3, y4;             // discard these
  is >> r.freq_ >> y1 >> r.power_ >> y3 >> y4;
  return is;
}

void EqualizationToolsDialog::impl::plot_current ()
{
  auto phase_graph = make_plot_data_loader (&plot_, 2, wrap_pi);
  plot_.graph (2)->data ()->clear ();
  std::generate_n (std::back_inserter (phase_graph), intervals + 1
                   , make_graph_generator (make_polynomial (current_coefficients_), freq_scaling, pi_scaling));

  auto group_delay_graph = make_plot_data_loader (&plot_, 3, adjust_identity);
  plot_.graph (3)->data ()->clear ();
  std::generate_n (std::back_inserter (group_delay_graph), intervals + 1
                   , make_graph_generator (make_group_delay (current_coefficients_), freq_scaling, identity<double>));
  plot_.graph (3)->rescaleValueAxis ();

  QFileInfo refspec_file_info {data_directory_.absoluteFilePath ("refspec.dat")};
  std::ifstream refspec_file (refspec_file_info.absoluteFilePath ().toLocal8Bit ().constData (), std::ifstream::in);
  unsigned n;
  if (refspec_file >> amp_poly_low_ >> amp_poly_high_ >> n)
    {
      std::istream_iterator<double> isi {refspec_file};
      amp_coefficients_.clear ();
      std::copy_n (isi, n, std::back_inserter (amp_coefficients_));
    }
  else
    {
      // may be old format refspec.dat with no header so rewind
      refspec_file.clear ();
      refspec_file.seekg (0);
    }

  auto reference_spectrum_graph = make_plot_data_loader (&plot_, 4, adjust_identity);
  plot_.graph (4)->data ()->clear ();
  std::copy (std::istream_iterator<PowerSpectrumPoint> {refspec_file},
             std::istream_iterator<PowerSpectrumPoint> {},
             std::back_inserter (reference_spectrum_graph));
  plot_.graph (4)->rescaleValueAxis (true);

  plot_.replot ();
}

struct PhasePoint
{
  operator QCPGraphData () const
  {
    return QCPGraphData {freq_, phase_};
  }

  double freq_;
  double phase_;
};

// read a phase point line from a stream (pcoeff file)
std::istream& operator >> (std::istream& is, PhasePoint& c)
{
  double pp, sigmay;            // discard these
  if (is >> c.freq_ >> pp >> c.phase_ >> sigmay)
    {
      c.freq_ = 1500. + 1000. * c.freq_; // scale frequency to Hz
      c.phase_ /= PI;                    // scale to units of Pi
    }
  return is;
}

void EqualizationToolsDialog::impl::plot_phase ()
{
  auto const& phase_file_name = QFileDialog::getOpenFileName (this
                                                              , "Select Phase Response Coefficients"
                                                              , data_directory_.absolutePath ()
                                                              , "Phase Coefficient Files (*.pcoeff)");
  if (!phase_file_name.size ()) return;

  std::ifstream phase_file (phase_file_name.toLocal8Bit ().constData (), std::ifstream::in);
  int n;
  float chi;
  float rmsdiff;
  unsigned freq_low;
  unsigned freq_high;
  unsigned terms;
  // read header information
  if (phase_file >> n >> chi >> rmsdiff >> freq_low >> freq_high >> terms)
    {
      std::istream_iterator<double> isi {phase_file};
      new_coefficients_.clear ();
      std::copy_n (isi, terms, std::back_inserter (new_coefficients_));

      if (phase_file)
        {
          plot_.graph (0)->data ()->clear ();
          plot_.graph (1)->data ()->clear ();

          // read the phase data and plot as graph 0
          auto graph = make_plot_data_loader (&plot_, 0, adjust_identity);
          std::copy_n (std::istream_iterator<PhasePoint> {phase_file},
                       intervals + 1, std::back_inserter (graph));

          if (phase_file)
            {
              plot_.graph(0)->setLineStyle(QCPGraph::lsNone);
              plot_.graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 4));
              plot_.graph (0)->setVisible (true);
              plot_.graph (0)->addToLegend ();

              // generate the proposed polynomial plot as graph 1
              auto graph = make_plot_data_loader (&plot_, 1, wrap_pi);
              std::generate_n (std::back_inserter (graph), intervals + 1
                               , make_graph_generator (make_polynomial (new_coefficients_)
                                                       , freq_scaling, pi_scaling));
              plot_.graph (1)->setVisible (true);
              plot_.graph (1)->addToLegend ();
            }

          plot_.replot ();
        }
    }
}

#include "moc_EqualizationToolsDialog.cpp"
