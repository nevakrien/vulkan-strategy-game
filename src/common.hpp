#ifndef COMMON_HPP
#define COMMON_HPP
#include <cassert>
#include <algorithm>
#include <vector>


#ifndef NDEBUG
  #define DEBUG_ASSERT(cond) assert(cond)
#else
  #define DEBUG_ASSERT(cond) ((void)(cond))
#endif


template <typename T>
inline bool vec_contains(const std::vector<T>& vec, const T& value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}


#endif // COMMON_HPP

