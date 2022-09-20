/**
 * MIT License
 *
 * Copyright (c) 2022 Erik Friesen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MICRO_FS_CONFIG_H
#define MICRO_FS_CONFIG_H

#include <assert.h>
#include "Unity.h"

#define UFAT_DEBUG(x) // printf x
#define UFAT_ERROR(x) printf x
#define UFAT_INFO(x)  // printf x
#define UFAT_INFO_SNPRINT(x) snprintf x

#define UFAT_RAND rand

int traceHandler(const char *format, ...);
#ifdef TRACE_ENABLE
#define UFAT_TRACE(x) traceHandler x
#else
#define UFAT_TRACE(x)
#endif

#define UFAT_ASSERT TEST_ASSERT

void assertHandler(char *file, int line);
#define UFAT_ASSERTs(expr)                                                    \
  if (!(expr))                                                                 \
  assertHandler(__FILE__, __LINE__)

#endif