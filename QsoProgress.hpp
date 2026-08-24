#ifndef QSOPROGRESS_HPP
#define QSOPROGRESS_HPP

// Values 0..5 cross the C++/Fortran boundary through nQSOProgress and index
// decoder AP tables. Their numeric values are part of the decoder contract.
enum class QsoProgress : int
{
  Calling = 0,
  Replying = 1,
  Report = 2,
  RogerReport = 3,
  Rogers = 4,
  Signoff = 5
};

#endif // QSOPROGRESS_HPP
