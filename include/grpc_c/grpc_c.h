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


#ifndef GRPC_C_PUBLIC_H
#define GRPC_C_PUBLIC_H

#include <stdlib.h>

typedef struct grpc_channel GRPC_channel;
/* The GRPC_status type is exposed to the end user */
typedef struct GRPC_status GRPC_status;
typedef struct grpc_client_context GRPC_client_context;
typedef struct grpc_completion_queue GRPC_completion_queue;

typedef struct grpc_client_reader_writer GRPC_client_reader_writer;
typedef struct grpc_client_reader GRPC_client_reader;
typedef struct grpc_client_writer GRPC_client_writer;
typedef struct grpc_client_async_reader_writer GRPC_client_async_reader_writer;
typedef struct grpc_client_async_reader GRPC_client_async_reader;
typedef struct grpc_client_async_writer GRPC_client_async_writer;
typedef struct grpc_client_async_response_reader GRPC_client_async_response_reader;

typedef struct grpc_method {
  enum RpcType {
    NORMAL_RPC = 0,
    CLIENT_STREAMING,  /* request streaming */
    SERVER_STREAMING,  /* response streaming */
    BIDI_STREAMING
  } type;
  const char* name;
} grpc_method;

typedef struct grpc_method GRPC_method;

/* For C compilers without bool support */
#ifndef __cplusplus
#if defined(__STDC__) && __STDC_VERSION__ >= 199901L
#include <stdbool.h>
#else
#ifndef bool
typedef enum _bool { false, true };
typedef enum _bool bool;
#endif
#endif
#endif

#include <grpc_c/message.h>

#endif /* GRPC_C_PUBLIC_H */
