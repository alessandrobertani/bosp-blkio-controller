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
 *
 * This source is based on code from the GNU ISO C++ Library.
 * Copyright (C) 2008, 2009 Free Software Foundation, Inc.
 */

#ifndef BBQUE_GNU_CHRONO_H_
#define BBQUE_GNU_CHRONO_H_

#include "bbque/cpp11/ratio.h"

#include <stdint.h>

#include <ctime>
#include <limits>
#include <type_traits>

namespace std {

namespace chrono {

    template<typename _Rep, typename _Period = ratio<1>>
      struct duration;

    template<typename _Clock, typename _Duration = typename _Clock::duration>
      struct time_point;

}

  // 20.8.2.3 specialization of common_type (for duration)
  template<typename _Rep1, typename _Period1, typename _Rep2, typename _Period2>
    struct common_type<chrono::duration<_Rep1, _Period1>,
                       chrono::duration<_Rep2, _Period2>>
    {
      typedef chrono::duration<typename common_type<_Rep1, _Rep2>::type,
        ratio<__static_gcd<_Period1::num, _Period2::num>::value,
        (_Period1::den / __static_gcd<_Period1::den, _Period2::den>::value)
        * _Period2::den>> type;
    };
  
  // 20.8.2.3 specialization of common_type (for time_point)
  template<typename _Clock, typename _Duration1, typename _Duration2>
    struct common_type<chrono::time_point<_Clock, _Duration1>,
                       chrono::time_point<_Clock, _Duration2>>
    {
      typedef chrono::time_point<_Clock, 
        typename common_type<_Duration1, _Duration2>::type> type;
    };

namespace chrono  {

    // Primary template for duration_cast impl.
    template<typename _ToDuration, typename _CF, typename _CR,
             bool _NumIsOne = false, bool _DenIsOne = false>
      struct __duration_cast_impl
      {
        template<typename _Rep, typename _Period>
          static _ToDuration __cast(const duration<_Rep, _Period>& __d)
          {
            return _ToDuration(static_cast<
              typename _ToDuration::rep>(static_cast<_CR>(__d.count())
              * static_cast<_CR>(_CF::num)
              / static_cast<_CR>(_CF::den)));
          }
      };

    template<typename _ToDuration, typename _CF, typename _CR>
      struct __duration_cast_impl<_ToDuration, _CF, _CR, true, true>
      {
        template<typename _Rep, typename _Period>
          static _ToDuration __cast(const duration<_Rep, _Period>& __d)
          {
            return _ToDuration(
              static_cast<typename _ToDuration::rep>(__d.count()));
          }
      };

    template<typename _ToDuration, typename _CF, typename _CR>
      struct __duration_cast_impl<_ToDuration, _CF, _CR, true, false>
      {
        template<typename _Rep, typename _Period>
          static _ToDuration __cast(const duration<_Rep, _Period>& __d)
          {
            return _ToDuration(static_cast<typename _ToDuration::rep>(
              static_cast<_CR>(__d.count()) / static_cast<_CR>(_CF::den))); 
          }
      };

    template<typename _ToDuration, typename _CF, typename _CR>
      struct __duration_cast_impl<_ToDuration, _CF, _CR, false, true>
      {
        template<typename _Rep, typename _Period>
          static _ToDuration __cast(const duration<_Rep, _Period>& __d)
          {
            return _ToDuration(static_cast<typename _ToDuration::rep>(
              static_cast<_CR>(__d.count()) * static_cast<_CR>(_CF::num)));
          }
      };

    /// duration_cast
    template<typename _ToDuration, typename _Rep, typename _Period>
      inline _ToDuration
      duration_cast(const duration<_Rep, _Period>& __d)
      {
        typedef typename
          ratio_divide<_Period, typename _ToDuration::period>::type __cf;
        typedef typename
          common_type<typename _ToDuration::rep, _Rep, intmax_t>::type __cr;

        return __duration_cast_impl<_ToDuration, __cf, __cr,
          __cf::num == 1, __cf::den == 1>::__cast(__d);
      }

    /// treat_as_floating_point
    template<typename _Rep>
      struct treat_as_floating_point 
      : is_floating_point<_Rep>
      { };


    /// duration_values
    template<typename _Rep>
      struct duration_values
      {
        static const _Rep
        zero()
        { return _Rep(0); }
        
        static const _Rep
        max()
        { return numeric_limits<_Rep>::max(); }
        
        static const _Rep
        min()
        { return numeric_limits<_Rep>::min(); }
      };

    template<typename _Tp>
      struct __is_duration
      : std::false_type
      { };

    template<typename _Rep, typename _Period>
      struct __is_duration<duration<_Rep, _Period>>
      : std::true_type
      { };

    template<typename T>
      struct __is_ratio
      : std::false_type
      { };

    template<intmax_t _Num, intmax_t _Den>
      struct __is_ratio<ratio<_Num, _Den>>
      : std::true_type
      { };

    /// duration
    template<typename _Rep, typename _Period>
      struct duration
      {
	static_assert(!__is_duration<_Rep>::value, "rep cannot be a duration");
	static_assert(__is_ratio<_Period>::value, 
		      "period must be a specialization of ratio");
        static_assert(_Period::num > 0, "period must be positive");
        
        typedef _Rep    rep;
        typedef _Period period;
        
        // 20.8.3.1 construction / copy / destroy
	duration() = default;

        template<typename _Rep2>
          explicit duration(_Rep2 const& __rep)
          : __r(static_cast<rep>(__rep))
          {
            static_assert(is_convertible<_Rep2,rep>::value 
			  && (treat_as_floating_point<rep>::value 
			      || !treat_as_floating_point<_Rep2>::value),
	      "cannot construct integral duration with floating point type");
          }

        template<typename _Rep2, typename _Period2>
          duration(const duration<_Rep2, _Period2>& __d)
          : __r(duration_cast<duration>(__d).count())
          {
            static_assert(treat_as_floating_point<rep>::value == true 
              || ratio_divide<_Period2, period>::type::den == 1, 
              "the resulting duration is not exactly representable");
          }

	~duration() = default;
	duration(const duration&) = default;
	duration& operator=(const duration&) = default;

        // 20.8.3.2 observer
        rep
        count() const
        { return __r; }

        // 20.8.3.3 arithmetic
        duration
        operator+() const 
        { return *this; }

        duration
        operator-() const 
        { return duration(-__r); }

        duration&
        operator++() 
        {
          ++__r;
          return *this;
        }

        duration
        operator++(int) 
        { return duration(__r++); }

        duration&
        operator--() 
        { 
          --__r;
          return *this;
        }

        duration
        operator--(int) 
        { return duration(__r--); }
        
        duration&
        operator+=(const duration& __d)
        {
          __r += __d.count();
          return *this;
        }

        duration&
        operator-=(const duration& __d)
        {
          __r -= __d.count();
          return *this;
        }

        duration&
        operator*=(const rep& __rhs)
        {
          __r *= __rhs;
          return *this;
        }

        duration&
        operator/=(const rep& __rhs)
        { 
          __r /= __rhs;
          return *this;
        }

        // 20.8.3.4 special values
        // TODO: These should be constexprs.
        static const duration
        zero()
        { return duration(duration_values<rep>::zero()); }

        static const duration
        min()
        { return duration(duration_values<rep>::min()); }
      
        static const duration
        max()
        { return duration(duration_values<rep>::max()); }
   
      private:    
        rep __r;
      };

    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline typename common_type<duration<_Rep1, _Period1>, 
                                  duration<_Rep2, _Period2>>::type
      operator+(const duration<_Rep1, _Period1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      {
        typedef typename common_type<duration<_Rep1, _Period1>, 
                                     duration<_Rep2, _Period2>>::type __ct;
        return __ct(__lhs) += __rhs;
      }

    template<typename _Rep1, typename _Period1, 
             typename _Rep2, typename _Period2>
      inline typename common_type<duration<_Rep1, _Period1>, 
                                  duration<_Rep2, _Period2>>::type
      operator-(const duration<_Rep1, _Period1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      {
        typedef typename common_type<duration<_Rep1, _Period1>,
                                     duration<_Rep2, _Period2>>::type __ct;
        return __ct(__lhs) -= __rhs;
      }

    template<typename _Rep1, typename _Period, typename _Rep2>
      inline duration<typename common_type<_Rep1, _Rep2>::type, _Period>
      operator*(const duration<_Rep1, _Period>& __d, const _Rep2& __s)
      {
        typedef typename common_type<_Rep1, _Rep2>::type __cr;
        return duration<__cr, _Period>(__d) *= __s;
      }

    template<typename _Rep1, typename _Period, typename _Rep2>
      inline duration<typename common_type<_Rep1, _Rep2>::type, _Period>
      operator*(const _Rep2& __s, const duration<_Rep1, _Period>& __d)
      { return __d * __s; }

    template<typename _Tp, typename _Up, typename _Ep = void>
      struct __division_impl;
  
    template<typename _Rep1, typename _Period, typename _Rep2>
      struct __division_impl<duration<_Rep1, _Period>, _Rep2, 
        typename enable_if<!__is_duration<_Rep2>::value>::type>
      {
        typedef typename common_type<_Rep1, _Rep2>::type __cr;
        typedef 
          duration<typename common_type<_Rep1, _Rep2>::type, _Period> __rt;

        static __rt
        __divide(const duration<_Rep1, _Period>& __d, const _Rep2& __s)
        { return duration<__cr, _Period>(__d) /= __s; }
      };

    template<typename _Rep1, typename _Period1, 
             typename _Rep2, typename _Period2>
      struct __division_impl<duration<_Rep1, _Period1>, 
                             duration<_Rep2, _Period2>>
      {
        typedef typename common_type<duration<_Rep1, _Period1>, 
                                     duration<_Rep2, _Period2>>::type __ct;
        typedef typename common_type<_Rep1, _Rep2>::type __rt;

        static __rt
        __divide(const duration<_Rep1, _Period1>& __lhs, 
                 const duration<_Rep2, _Period2>& __rhs)
        { return __ct(__lhs).count() / __ct(__rhs).count(); }
      };
  
    template<typename _Rep, typename _Period, typename _Up>
      inline typename __division_impl<duration<_Rep, _Period>, _Up>::__rt
      operator/(const duration<_Rep, _Period>& __d, const _Up& __u)
      {
        return 
          __division_impl<duration<_Rep, _Period>, _Up>::__divide(__d, __u);
      }
 
    // comparisons
    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline bool
      operator==(const duration<_Rep1, _Period1>& __lhs, 
                 const duration<_Rep2, _Period2>& __rhs)
      {
        typedef typename common_type<duration<_Rep1, _Period1>, 
                                     duration<_Rep2, _Period2>>::type __ct;
        return __ct(__lhs).count() == __ct(__rhs).count();
      }

    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline bool
      operator<(const duration<_Rep1, _Period1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      {
        typedef typename common_type<duration<_Rep1, _Period1>, 
                                     duration<_Rep2, _Period2>>::type __ct;
        return __ct(__lhs).count() < __ct(__rhs).count();
      }

    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline bool
      operator!=(const duration<_Rep1, _Period1>& __lhs, 
                 const duration<_Rep2, _Period2>& __rhs)
      { return !(__lhs == __rhs); }

    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline bool
      operator<=(const duration<_Rep1, _Period1>& __lhs, 
                 const duration<_Rep2, _Period2>& __rhs)
      { return !(__rhs < __lhs); }

    template<typename _Rep1, typename _Period1,
             typename _Rep2, typename _Period2>
      inline bool 
      operator>(const duration<_Rep1, _Period1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      { return __rhs < __lhs; }

    template<typename _Rep1, typename _Period1, 
             typename _Rep2, typename _Period2>
      inline bool
      operator>=(const duration<_Rep1, _Period1>& __lhs, 
                 const duration<_Rep2, _Period2>& __rhs)
      { return !(__lhs < __rhs); }

    /// nanoseconds
    typedef duration<int64_t,        nano> nanoseconds;

    /// microseconds
    typedef duration<int64_t,       micro> microseconds;

    /// milliseconds
    typedef duration<int64_t,       milli> milliseconds;
    
    /// seconds
    typedef duration<int64_t             > seconds;

    /// minutes
    typedef duration<int,     ratio<  60>> minutes;

    /// hours
    typedef duration<int,     ratio<3600>> hours;

    /// time_point
    template<typename _Clock, typename _Duration>
      struct time_point
      {
	typedef _Clock                    clock;
	typedef _Duration                 duration;
	typedef typename duration::rep    rep;
	typedef typename duration::period period;

	time_point() : __d(duration::zero())
	{ }

	explicit time_point(const duration& __dur) 
	: __d(duration::zero() + __dur)
	{ }

	// conversions
	template<typename _Duration2>
	  time_point(const time_point<clock, _Duration2>& __t)
	  : __d(__t.time_since_epoch())
	  { }

	// observer
	duration
	time_since_epoch() const
	{ return __d; }
	
	// arithmetic
	time_point&
	operator+=(const duration& __dur)
	{
	  __d += __dur;
	  return *this;
	}
	
	time_point&
	operator-=(const duration& __dur)
	{
	  __d -= __dur;
	  return *this;
	}
	
	// special values
	// TODO: These should be constexprs.
	static const time_point
	min()
	{ return time_point(duration::min()); }
	
	static const time_point
	max()
	{ return time_point(duration::max()); }
	
      private:
	duration __d;
      };
  
    /// time_point_cast
    template<typename _ToDuration, typename _Clock, typename _Duration>
      inline time_point<_Clock, _ToDuration> 
      time_point_cast(const time_point<_Clock, _Duration>& __t)
      {
        return time_point<_Clock, _ToDuration>(
          duration_cast<_ToDuration>(__t.time_since_epoch()));  
      }

    template<typename _Clock, typename _Duration1,
             typename _Rep2, typename _Period2>
      inline time_point<_Clock, 
        typename common_type<_Duration1, duration<_Rep2, _Period2>>::type>
      operator+(const time_point<_Clock, _Duration1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      {
        typedef time_point<_Clock, 
          typename common_type<_Duration1, 
                               duration<_Rep2, _Period2>>::type> __ct;
        return __ct(__lhs) += __rhs;
      }

    template<typename _Rep1, typename _Period1,
             typename _Clock, typename _Duration2>
      inline time_point<_Clock, 
        typename common_type<duration<_Rep1, _Period1>, _Duration2>::type>
      operator+(const duration<_Rep1, _Period1>& __lhs, 
                const time_point<_Clock, _Duration2>& __rhs)
      { return __rhs + __lhs; }

    template<typename _Clock, typename _Duration1,
             typename _Rep2, typename _Period2>
      inline time_point<_Clock, 
        typename common_type<_Duration1, duration<_Rep2, _Period2>>::type>
      operator-(const time_point<_Clock, _Duration1>& __lhs, 
                const duration<_Rep2, _Period2>& __rhs)
      { return __lhs + (-__rhs); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline typename common_type<_Duration1, _Duration2>::type
      operator-(const time_point<_Clock, _Duration1>& __lhs, 
                const time_point<_Clock, _Duration2>& __rhs)
      { return __lhs.time_since_epoch() - __rhs.time_since_epoch(); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator==(const time_point<_Clock, _Duration1>& __lhs,
                 const time_point<_Clock, _Duration2>& __rhs)
      { return __lhs.time_since_epoch() == __rhs.time_since_epoch(); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator!=(const time_point<_Clock, _Duration1>& __lhs,
                 const time_point<_Clock, _Duration2>& __rhs)
      { return !(__lhs == __rhs); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator<(const time_point<_Clock, _Duration1>& __lhs,
                const time_point<_Clock, _Duration2>& __rhs)
      { return  __lhs.time_since_epoch() < __rhs.time_since_epoch(); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator<=(const time_point<_Clock, _Duration1>& __lhs,
                 const time_point<_Clock, _Duration2>& __rhs)
      { return !(__rhs < __lhs); }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator>(const time_point<_Clock, _Duration1>& __lhs,
                const time_point<_Clock, _Duration2>& __rhs)
      { return __rhs < __lhs; }

    template<typename _Clock, typename _Duration1, typename _Duration2>
      inline bool
      operator>=(const time_point<_Clock, _Duration1>& __lhs,
                 const time_point<_Clock, _Duration2>& __rhs)
      { return !(__lhs < __rhs); }

    /// system_clock
    struct system_clock
    {
#if defined(_GLIBCXX_USE_CLOCK_REALTIME) || defined(__BIONIC__)
      typedef chrono::nanoseconds     duration;      
#elif defined(_GLIBCXX_USE_GETTIMEOFDAY)
      typedef chrono::microseconds    duration;      
#else
      typedef chrono::seconds         duration;      
#endif

      typedef duration::rep    rep;
      typedef duration::period period;
      typedef chrono::time_point<system_clock, duration> time_point;

      static const bool is_steady = false;

      static time_point
      now() throw ();

      // Map to C API
      static std::time_t
      to_time_t(const time_point& __t)
      {
        return std::time_t(
          duration_cast<chrono::seconds>(__t.time_since_epoch()).count());
      }

      static time_point
      from_time_t(std::time_t __t)
      { 
        return time_point_cast<system_clock::duration>(
          chrono::time_point<system_clock, chrono::seconds>(
            chrono::seconds(__t)));
      }

      // TODO: requires constexpr
      /*  
      static_assert(
        system_clock::duration::min() < 
        system_clock::duration::zero(), 
        "a clock's minimum duration cannot be less than its epoch");
      */
    };

#if defined(_GLIBCXX_USE_CLOCK_MONOTONIC) || defined(__BIONIC__)
    /// steady_clock
    struct steady_clock
    {
      typedef chrono::nanoseconds duration;
      typedef duration::rep       rep;
      typedef duration::period    period;
      typedef chrono::time_point<steady_clock, duration> time_point;

      static const bool is_steady = true;

      static time_point
      now();
    };
#else
    typedef system_clock steady_clock;
#endif

    typedef system_clock high_resolution_clock;

} // namespace chrono

} // namespace std

#endif /* end of include guard: BBQUE_GNU_CHRONO_H_ */
