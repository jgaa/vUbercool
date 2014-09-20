#pragma once

/* Practical stuff for implementation use */

#include "war_basics.h"
#include "war_debug_helper.h"

#define WAR_LOCK std::lock_guard<std::mutex> __war_lock(lock_)
#define WAR_UNLOCK __war_lock.unlock();

/*! Lock the local object and another std::mutex simultaineously */
#define WAR_LOCK2(other) \
	std::unique_lock<std::mutex> __war_lock_a(lock_, std::defer_lock); \
	std::unique_lock<std::mutex> __war_lock_b(other, std::defer_lock); \
	std::lock(__war_lock_a, __war_lock_b)


/*! Lock the local object and two other std::mutex simultaineously */
#define WAR_LOCK3(first, second) \
	std::unique_lock<std::mutex> __war_lock_a(lock_, std::defer_lock); \
	std::unique_lock<std::mutex> __war_lock_b(first, std::defer_lock); \
	std::unique_lock<std::mutex> __war_lock_c(second, std::defer_lock); \
	std::lock(__war_lock_a, __war_lock_b, __war_lock_c)

// Always use the returned pointer. The supplied buffer may not be set!
#ifdef WIN32
#   define war_localtime(t, b) (localtime_s(b, t) == 0 ? b : 0)
#elif WIN32
#   define war_localtime(t, b) localtime(t)
#else
#   define war_localtime(t, b) localtime_r(t, b)
#endif

#ifdef WIN32
	// Microsoft loves to mess up the public name space with lame macros
#	ifdef min
#   	undef min
#	endif

// Microsoft loves to mess up the public name space with lame macros
#	ifdef max
#   	undef max
#	endif
#endif

