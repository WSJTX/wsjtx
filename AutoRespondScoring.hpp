#ifndef AUTORESPONDSCORING_HPP
#define AUTORESPONDSCORING_HPP

#include <limits>

class AutoRespondScores final
{
public:
  void reset()
  {
    m_maximumDistance = std::numeric_limits<int>::lowest();
    m_maximumDb = std::numeric_limits<int>::lowest();
    m_minimumDb = std::numeric_limits<int>::max();
  }

  bool wouldImproveDistance(int candidate) const
  {
    return candidate > m_maximumDistance;
  }

  bool wouldImproveMaximumDb(int candidate) const
  {
    return candidate > m_maximumDb;
  }

  bool wouldImproveMinimumDb(int candidate) const
  {
    return candidate < m_minimumDb;
  }

  bool considerDistance(int candidate)
  {
    if (!wouldImproveDistance(candidate)) return false;
    m_maximumDistance = candidate;
    return true;
  }

  bool considerMaximumDb(int candidate)
  {
    if (!wouldImproveMaximumDb(candidate)) return false;
    m_maximumDb = candidate;
    return true;
  }

  bool considerMinimumDb(int candidate)
  {
    if (!wouldImproveMinimumDb(candidate)) return false;
    m_minimumDb = candidate;
    return true;
  }

private:
  int m_maximumDistance {std::numeric_limits<int>::lowest()};
  int m_maximumDb {std::numeric_limits<int>::lowest()};
  int m_minimumDb {std::numeric_limits<int>::max()};
};

#endif
