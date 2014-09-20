 #pragma once

#include <functional>


#ifdef _MSC_VER
#	 pragma warning(disable : 4503 4250 4996)
#else
  // override not supported by g++
#   define override
#endif

namespace war {

typedef std::function<void ()> func_t;
typedef std::pair<func_t, const char *> task_t;

} // namespace

#include "war_error_handling.h"
