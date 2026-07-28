#ifndef AUTORESPONDSCORING_HPP
#define AUTORESPONDSCORING_HPP

#include <limits>

class AutoRespondScores final
{
public:
  void reset()
  {
    m_maximumDistance = 0;
    m_maximumDb = std::numeric_limits<int>::lowest();
    m_minimumDb = std::numeric_limits<int>::max();
  }

  bool considerDistance(int candidate)
  {
    if (candidate <= m_maximumDistance) return false;
    m_maximumDistance = candidate;
    return true;
  }

  bool considerMaximumDb(int candidate)
  {
    if (candidate <= m_maximumDb) return false;
    m_maximumDb = candidate;
    return true;
  }

  bool considerMinimumDb(int candidate)
  {
    if (candidate >= m_minimumDb) return false;
    m_minimumDb = candidate;
    return true;
  }

private:
  int m_maximumDistance {0};
  int m_maximumDb {std::numeric_limits<int>::lowest()};
  int m_minimumDb {std::numeric_limits<int>::max()};
};

#endif
