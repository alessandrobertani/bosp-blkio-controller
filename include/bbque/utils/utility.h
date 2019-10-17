/*
 * Copyright (C) 2012  Politecnico di Milano
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BBQUE_UTILITY_H_
#define BBQUE_UTILITY_H_

#include <assert.h>
#include <memory>
#include <unistd.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <string>

#include <sys/prctl.h>
#include <sys/syscall.h>

#include <bbque/utils/timer.h>

#define COLOR_WHITE  "\033[1;37m"
#define COLOR_LGRAY  "\033[37m"
#define COLOR_GRAY   "\033[1;30m"
#define COLOR_BLACK  "\033[30m"
#define COLOR_RED    "\033[31m"
#define COLOR_LRED   "\033[1;31m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_LGREEN "\033[1;32m"
#define COLOR_BROWN  "\033[33m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_BLUE   "\033[34m"
#define COLOR_LBLUE  "\033[1;34m"
#define COLOR_PURPLE "\033[35m"
#define COLOR_PINK   "\033[1;35m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_LCYAN  "\033[1;36m"

// Setup default logger configuration
#ifndef BBQUE_LOG_MODULE
# define BBQUE_LOG_MODULE MODULE_NAMESPACE
#endif

#if defined(BBQUE_APP)
//*****************************************************************************
//*  Logging routines for RTLib and Managed Applications
//*****************************************************************************

#ifndef BBQUE_LOG_UID
# define BBQUE_LOG_UID "*****"
#endif

// Define a 'generic logger', which can be customized based on the previously
// defined set of macros
# define BBQUE_FMT(color, level, fmt) \
	"\033[0m%011.6f %-19.19s %c %-8.8s: " color fmt "\033[0m", \
	bbque_tmr.getElapsedTime(), \
	BBQUE_LOG_UID, \
	level, \
	BBQUE_LOG_MODULE

// Partially specialize the 'generic logger' using different (log-level, color)
# define FD(fmt) BBQUE_FMT(COLOR_LGRAY,  'D', fmt)
# define FI(fmt) BBQUE_FMT(COLOR_GREEN,  'I', fmt)
# define FN(fmt) BBQUE_FMT(COLOR_CYAN,   'N', fmt)
# define FW(fmt) BBQUE_FMT(COLOR_YELLOW, 'W', fmt)
# define FE(fmt) BBQUE_FMT(COLOR_RED,    'E', fmt)

#else
//*****************************************************************************
//*  Logging routines for BarbequeRTRM modules
//*****************************************************************************

// Define a generic logger, which can be customized based on the previously
// defined set of macros
# define BBQUE_FMT(color, level, fmt) \
	"\033[0m%-23.23s - %-6.6s %-16.16s: " color fmt "\033[0m", \
	"*****", \
	level, \
	BBQUE_LOG_MODULE

// Partially specialize the 'generi logger' using different (log-level, color)
# define FD(fmt) BBQUE_FMT(COLOR_LGRAY,  "DEBUG",   fmt)
# define FI(fmt) BBQUE_FMT(COLOR_GREEN,  "INFO",    fmt)
# define FN(fmt) BBQUE_FMT(COLOR_CYAN,   "NOTICE",  fmt)
# define FW(fmt) BBQUE_FMT(COLOR_YELLOW, "WARNING", fmt)
# define FE(fmt) BBQUE_FMT(COLOR_RED,    "ERROR",   fmt)

#endif

#ifdef BBQUE_DEBUG
# define DB(x) x
#else
# define DB(x)
#endif

/** Get the pointer to the containing structure */
#define container_of(ptr, type, member)\
	(type*)((char*)(ptr) - offsetof(type, member))

/** Get number of entries of the specified array */
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

/** Get a string lenght at compile time (null terminator not counted) */
#define STRLEN(s) ((sizeof(s)/sizeof(s[0]))-1)

/** Stringify the result of expansion of a macro argument
 * This requires to use two levels of macros.*/
#define STR(s) XSTR(s)
#define XSTR(s) #s

/** Optimize branch prediction for "taken" */
#define likely(x)       __builtin_expect((x),1)
/** Optimize branch prediction for "untaken" */
#define unlikely(x)     __builtin_expect((x),0)

/** Silence the unused variable warning for a specific variable*/
#define UNUSED(x) (void)(x);

#ifndef CONFIG_TARGET_ANDROID
/** Return the PID of the calling process/thread */
inline pid_t gettid()
{
	return syscall(SYS_gettid);
}
#endif

#define BBQUE_MODULE_NAME(STR) "bq." STR

/** The High-Resolution timer exported by either the Barbeque and the RTLib */
extern bbque::utils::Timer bbque_tmr;

/**
 * Comparison between shared pointer objects.
 * This is performed by forwarding the call to the operator '<' of the pointed
 * class type
 */
template<class T>
class CompareSP
{
public:
	bool operator() (
	        const std::shared_ptr<T> & sp1,
	        const std::shared_ptr<T> & sp2) const {
		return *sp1 < *sp2;
	}
};

inline bool IsNumber(const std::string & str)
{
	size_t p = 0;
	while (p < str.length())
		if (!isdigit(str[p++])) return false;
	return true;
}

/**
 * @brief Compute a string hash, possibly at compile-time. This is
 *        usually used in switch{} statement that wants a const
 *        char* arguments.
 * @param The input string to be hashed
 * @see http://stackoverflow.com/a/16388610/835146
 */
unsigned constexpr ConstHashString(char const *input)
{
	return *input ?
	       static_cast<unsigned int>(*input) + 33 * ConstHashString(input + 1) :
	       5381;
}


#endif // BBQUE_UTILITY_H_
