/*
Copyright 2015 Glen Joseph Fernandes
(glenjofe@gmail.com)
Distributed under the Boost Software License, Version 1.0.
(http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef SRC_COMMON_FAST_ALIGN_H_
#define SRC_COMMON_FAST_ALIGN_H_

#include <cstdint>

namespace dingofs {
namespace utils {

namespace detail {

template <typename T, typename U>
struct not_pointer {
  typedef U type;
};

template <typename T, typename U>
struct not_pointer<T*, U> {};

}  // namespace detail

template <typename T>
constexpr inline typename detail::not_pointer<T, T>::type align_down(
    T value, std::size_t alignment) noexcept {
  return T(value & ~T(alignment - 1));
}

inline void* align_down(void* ptr, std::size_t alignment) noexcept {
  return (void*)(align_down((std::size_t)ptr, alignment));  // NOLINT
}

template <typename T>
constexpr inline typename detail::not_pointer<T, T>::type align_up(
    T value, std::size_t alignment) noexcept {
  return T((value + (T(alignment) - 1)) & ~T(alignment - 1));
}

inline void* align_up(void* ptr, std::size_t alignment) noexcept {
  return (void*)(align_up((std::size_t)ptr, alignment));  // NOLINT
}

template <typename T>
constexpr inline typename detail::not_pointer<T, bool>::type is_aligned(
    T value, std::size_t alignment) noexcept {
  return (value & (T(alignment) - 1)) == 0;
}

inline bool is_aligned(const void* ptr, std::size_t alignment) noexcept {
  return is_aligned((std::size_t)ptr, alignment);  // NOLINT
}

constexpr inline bool is_alignment(std::size_t value) noexcept {
  return (value > 0) && ((value & (value - 1)) == 0);
}

}  // namespace utils
}  // namespace dingofs

#endif  // SRC_COMMON_FAST_ALIGN_H_
