#ifndef QT_HELPERS_HPP_
#define QT_HELPERS_HPP_

#include <stdexcept>

#include <QString>
#include <QChar>
#include <QMetaObject>
#include <QHostAddress>
#include <QDataStream>
#include <QMetaType>
#include <QMetaEnum>

#define ENUM_QDATASTREAM_OPS_DECL(CLASS, ENUM)				\
  QDataStream& operator << (QDataStream&, CLASS::ENUM const&);			\
  QDataStream& operator >> (QDataStream&, CLASS::ENUM&);

#define ENUM_QDATASTREAM_OPS_IMPL(CLASS, ENUM)				\
  QDataStream& operator << (QDataStream& os, CLASS::ENUM const& v)		\
  {									\
    auto const& mo = CLASS::staticMetaObject;				\
    return os << mo.enumerator (mo.indexOfEnumerator (#ENUM)).valueToKey (static_cast<int> (v)); \
  }									\
									\
  QDataStream& operator >> (QDataStream& is, CLASS::ENUM& v)		\
  {									\
    char * buffer;							\
    is >> buffer;							\
    bool ok {false};							\
    auto const& mo = CLASS::staticMetaObject;				\
    auto const& me = mo.enumerator (mo.indexOfEnumerator (#ENUM)); \
    if (buffer)								\
      {									\
        v = static_cast<CLASS::ENUM> (me.keyToValue (buffer, &ok));	\
        delete [] buffer;                                           \
      }									\
    if (!ok)								\
      {									\
        v = static_cast<CLASS::ENUM> (me.value (0));  \
      }									\
    return is;								\
  }

#define ENUM_CONVERSION_OPS_DECL(CLASS, ENUM)	\
  QString enum_to_qstring (CLASS::ENUM const&);

#define ENUM_CONVERSION_OPS_IMPL(CLASS, ENUM)				\
  QString enum_to_qstring (CLASS::ENUM const& m)				\
  {									\
    auto const& mo = CLASS::staticMetaObject;				\
    return QString {mo.enumerator (mo.indexOfEnumerator (#ENUM)).valueToKey (static_cast<int> (m))}; \
  }

#if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
Qt::SplitBehaviorFlags const SkipEmptyParts = Qt::SkipEmptyParts;
#else
QString::SplitBehavior const SkipEmptyParts = QString::SkipEmptyParts;
#endif

inline
void throw_qstring (QString const& qs)
{
  throw std::runtime_error {qs.toLocal8Bit ().constData ()};
}

QString font_as_stylesheet (QFont const&);

// do what is necessary to change a dynamic property and trigger any
// conditional style sheet updates
void update_dynamic_property (QWidget *, char const * property, QVariant const& value);

// round a QDateTime instance to an interval
QDateTime qt_round_date_time_to (QDateTime dt, int seconds);

// truncate a QDateTime to an interval
QDateTime qt_truncate_date_time_to (QDateTime dt, int seconds);

template <class T>
class VPtr
{
public:
  static T * asPtr (QVariant v)
  {
    return reinterpret_cast<T *> (v.value<void *> ());
  }

  static QVariant asQVariant(T * ptr)
  {
    return QVariant::fromValue (reinterpret_cast<void *> (ptr));
  }
};

#if QT_VERSION < QT_VERSION_CHECK (5, 14, 0)
// The Qt  devs "fixed" this  in 5.14 to  specialize to use  their own
// qHash(), it doesn't  fix the problem we were  addressing as qHash()
// returns a  uint so is  still a  poorly distributed 32-bit  value on
// 64-bit platforms, but  we can't specialize ourselves  as Qt already
// has - sigh.
namespace std
{
  // std::hash<> specialization for QString based on the dbj2
  // algorithm http://www.cse.yorku.ca/~oz/hash.html because qHash()
  // is poor on 64-bit platforms due to being a 32-bit hash value
  template<>
  struct hash<QString>
  {
    std::size_t operator () (QString const& s) const noexcept
    {
      std::size_t hash {5381};
      for (int i = 0; i < s.size (); ++i)
        {
          hash = ((hash << 5) + hash) + ((s.at (i).row () << 8) | s.at (i).cell ());
        }
      return hash;
    }
  };
}
#endif

// Register some useful Qt types with QMetaType
Q_DECLARE_METATYPE (QHostAddress);

#endif
