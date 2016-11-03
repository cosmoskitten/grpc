/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_SUPPORT_ENDIAN_H
#define GRPC_SUPPORT_ENDIAN_H

#include <grpc/support/port_platform.h>

/* Only for HPUX now */
#ifdef GPR_HPUX
#define GPR_BIG_ENDIAN
#else
#define GPR_LITTLE_ENDIAN
#endif

#ifdef GPR_BIG_ENDIAN
 #if defined(__GNUC__) && (__GNUC__>4 || (__GNUC__==4 && __GNUC_MINOR__>=3))
  #define GPR_WORD_TO_NATIVE(N, W32)   N = __builtin_bswap32(W32)
 #else
  #define GPR_WORD_TO_NATIVE(N, W32)     \
    N = ((((W32) & 0x000000FF) << 24)  | \
      (((W32) & 0x0000FF00) <<  8)     | \
      (((W32) & 0x00FF0000) >>  8)     | \
      (((W32) & 0xFF000000) >> 24))
 #endif
#endif

#ifdef GPR_LITTLE_ENDIAN
  #define GPR_WORD_TO_NATIVE(N, W32)
#endif

#endif /* GRPC_SUPPORT_ENDIAN_H */
