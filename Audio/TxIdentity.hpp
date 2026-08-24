#ifndef TX_IDENTITY_HPP_
#define TX_IDENTITY_HPP_

#include <QMetaType>
#include <QtGlobal>

namespace TxEvidence
{
  struct TxSessionId
  {
    constexpr TxSessionId () noexcept : value_ {0} {}
    explicit constexpr TxSessionId (qint64 value) noexcept : value_ {value} {}

    static constexpr TxSessionId invalid () noexcept {return TxSessionId {};}
    constexpr bool isValid () const noexcept {return value_ > 0;}
    constexpr qint64 value () const noexcept {return value_;}

  private:
    qint64 value_;
  };

  struct TxGeneration
  {
    constexpr TxGeneration () noexcept : value_ {0} {}
    explicit constexpr TxGeneration (qint64 value) noexcept : value_ {value} {}

    static constexpr TxGeneration invalid () noexcept {return TxGeneration {};}
    constexpr bool isValid () const noexcept {return value_ > 0;}
    constexpr qint64 value () const noexcept {return value_;}

  private:
    qint64 value_;
  };

  inline constexpr bool operator == (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return lhs.value () == rhs.value ();
  }

  inline constexpr bool operator != (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return !(lhs == rhs);
  }

  inline constexpr bool operator < (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return lhs.value () < rhs.value ();
  }

  inline constexpr bool operator <= (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return !(rhs < lhs);
  }

  inline constexpr bool operator > (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return rhs < lhs;
  }

  inline constexpr bool operator >= (TxSessionId lhs, TxSessionId rhs) noexcept
  {
    return !(lhs < rhs);
  }

  inline constexpr bool operator == (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return lhs.value () == rhs.value ();
  }

  inline constexpr bool operator != (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return !(lhs == rhs);
  }

  inline constexpr bool operator < (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return lhs.value () < rhs.value ();
  }

  inline constexpr bool operator <= (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return !(rhs < lhs);
  }

  inline constexpr bool operator > (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return rhs < lhs;
  }

  inline constexpr bool operator >= (TxGeneration lhs, TxGeneration rhs) noexcept
  {
    return !(lhs < rhs);
  }
}

Q_DECLARE_METATYPE (TxEvidence::TxSessionId)
Q_DECLARE_METATYPE (TxEvidence::TxGeneration)

#endif
