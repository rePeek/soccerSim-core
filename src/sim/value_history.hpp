#ifndef FOOTBALL_SIM_VALUE_HISTORY_HPP
#define FOOTBALL_SIM_VALUE_HISTORY_HPP

#include <list>

// Simulation history sampled at the fixed 10 ms simulation tick.
template <typename T>
class ValueHistory {
 public:
  explicit ValueHistory(unsigned int max_time_ms = 10000)
      : max_time_ms_(max_time_ms) {}

  void Insert(const T& value) {
    values_.push_back(value);
    if (values_.size() > max_time_ms_ / 10) values_.pop_front();
  }

  T GetAverage(unsigned int time_ms) const {
    T total = 0;
    unsigned int count = 0;
    if (!values_.empty()) {
      auto iter = values_.end();
      --iter;
      while (count <= time_ms / 10) {
        total += *iter;
        ++count;
        if (iter == values_.begin()) break;
        --iter;
      }
    }
    if (count > 0) total /= static_cast<float>(count);
    return total;
  }

  void Clear() { values_.clear(); }
  unsigned int GetMaxTime_ms() const { return max_time_ms_; }
  const std::list<T>& GetValues() const { return values_; }
  void Restore(unsigned int max_time_ms, const std::list<T>& values) {
    max_time_ms_ = max_time_ms;
    values_ = values;
  }

 private:
  unsigned int max_time_ms_ = 0;
  std::list<T> values_;
};

#endif  // FOOTBALL_SIM_VALUE_HISTORY_HPP
