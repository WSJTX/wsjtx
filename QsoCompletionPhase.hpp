#ifndef QSOCOMPLETIONPHASE_HPP
#define QSOCOMPLETIONPHASE_HPP

enum class QsoCompletionPhase
{
  Ordinary,
  AwaitingFinal73,
  FinalReplyPending,
  TimedOut,
  Finished,
  Cancelled
};

#endif
