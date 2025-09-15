#ifndef COMMON_HPP
#define COMMON_HPP
#include <cassert>

#ifndef NDEBUG
  #define DEBUG_ASSERT(cond) assert(cond)
#else
  #define DEBUG_ASSERT(cond) ((void)(cond))
#endif


#endif // COMMON_HPP

