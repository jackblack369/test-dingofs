/*
 *  Copyright (c) 2020 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: dingo
 * File Created: Wednesday, 5th September 2018 7:23:56 pm
 * Author: tongguangxun
 */

#ifndef INCLUDE_DINGO_COMPILER_SPECIFIC_H_
#define INCLUDE_DINGO_COMPILER_SPECIFIC_H_

#define DINGO_CACHELINE_SIZE 64
#define DINGO_CACHELINE_ALIGNMENT alignas(DINGO_CACHELINE_SIZE)

#if defined(COMPILER_GCC)
#if defined(__cplusplus)
#define DINGO_LIKELY(expr) \
    (__builtin_expect(static_cast<bool>(expr), true))
#define DINGO_UNLIKELY(expr) \
    (__builtin_expect(static_cast<bool>(expr), false))
#else
#define DINGO_LIKELY(expr) (__builtin_expect(!!(expr), 1))
#define DINGO_UNLIKELY(expr) (__builtin_expect(!!(expr), 0))
#endif
#else
#define DINGO_LIKELY(expr) (expr)
#define DINGO_UNLIKELY(expr) (expr)
#endif  // defined(COMPILER_GCC)

#ifdef UNIT_TEST
#define DINGO_MOCK virtual
#else
#define DINGO_MOCK
#endif  // UNIT_TEST

// FALLTHROUGH_INTENDED

#if defined(__GNUC__) && __GNUC__ >= 7
#define FALLTHROUGH_INTENDED __attribute__((fallthrough))
#else
#define FALLTHROUGH_INTENDED ((void)0)
#endif /* __GNUC__ >= 7 */

#endif  // INCLUDE_DINGO_COMPILER_SPECIFIC_H_
