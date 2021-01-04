#ifndef MULTI_GEOMETRY_WIDGET_HPP__
#define MULTI_GEOMETRY_WIDGET_HPP__

#include <cstddef>
#include <utility>
#include <array>

#include <QWidget>
#include <QByteArray>
#include <QEvent>

//
// Class MultiGeometryWidget<N, Widget> - Decorate a QWidget type with
// 																				switchable geometries
//
// The abstract base class imbues a Qt Widget type with N alternative
// geometries. Sub-classes can initialise the currently selected
// geometry and the initial geometries using geometries. To switch
// geometry call select_geometry(n) which saves the current geometry
// and switches to the n'th saved geometry.
//
template<std::size_t N, typename Widget=QWidget>
class MultiGeometryWidget
  : public Widget
{
public:
  template<typename... Args>
  explicit MultiGeometryWidget (Args&&... args)
    : Widget {std::forward<Args> (args)...}
    , current_geometry_ {0}
  {
  }

  void geometries (std::size_t current
                   , std::array<QByteArray, N> const& the_geometries = std::array<QByteArray, N> {})
  {
    Q_ASSERT (current < the_geometries.size ());
    saved_geometries_ = the_geometries;
    current_geometry_ = current;
    Widget::restoreGeometry (saved_geometries_[current_geometry_]);
  }

  std::array<QByteArray, N> const& geometries () const {return saved_geometries_;}
  std::size_t current () {return current_geometry_;}

  // Call this to select a new geometry denoted by the 'n' argument,
  // any actual layout changes should be made in the implementation of
  // the change_layout operation below.
  void select_geometry (std::size_t n)
  {
    Q_ASSERT (n < N);
    auto geometry = Widget::saveGeometry ();
    change_layout (n);
    saved_geometries_[current_geometry_] = geometry;
    current_geometry_ = n;

    // Defer restoration of the window geometry until the layour
    // request event has been processed, this is necessary otherwise
    // the final geometry may be affected by widgets not shown in the
    // new layout.
    desired_geometry_ = saved_geometries_[n];
  }

protected:
  virtual ~MultiGeometryWidget () {}

private:
  // Override this operation to implement any layout changes for the
  // geometry specified by the argument 'n'.
  virtual void change_layout (std::size_t n) = 0;

  bool event (QEvent * e) override
  {
    auto ret = Widget::event (e);
    if (QEvent::LayoutRequest == e->type ()
        && desired_geometry_.size ())
      {
        // Restore the new desired geometry and flag that we have done
        // so by clearing the desired_geometry_ member variable.
        QByteArray geometry;
        std::swap (geometry, desired_geometry_);
        Widget::restoreGeometry (geometry);
      }
    return ret;
  }

  std::size_t current_geometry_;
  std::array<QByteArray, N> saved_geometries_;
  QByteArray desired_geometry_;
};

#endif
