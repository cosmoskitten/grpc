/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/iomgr/resolve_address.h"

#include <string.h>
#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/executor.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(100);
}

typedef struct args_struct {
  gpr_event ev;
  grpc_resolved_addresses *addrs;
} args_struct;

void args_init(args_struct *args) {
  gpr_event_init(&args->ev);
  args->addrs = NULL;
}

void args_finish(args_struct *args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  grpc_resolved_addresses_destroy(args->addrs);
}

static void must_succeed(grpc_exec_ctx *exec_ctx, void *argsp,
                         grpc_error *err) {
  args_struct *args = argsp;
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != NULL);
  GPR_ASSERT(args->addrs->naddrs > 0);
  gpr_event_set(&args->ev, (void *)1);
}

static void must_fail(grpc_exec_ctx *exec_ctx, void *argsp, grpc_error *err) {
  args_struct *args = argsp;
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  gpr_event_set(&args->ev, (void *)1);
}

static void test_unix_socket(void) {
  args_struct args;
  args_init(&args);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resolve_address(&exec_ctx, "unix:/path/name", NULL,
                       grpc_closure_create(must_succeed, &args), &args.addrs);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

static void test_unix_socket_path_name_too_long(void) {
  args_struct args;
  args_init(&args);
  const char prefix[] = "unix:/path/name";
  size_t path_name_length =
      GPR_ARRAY_SIZE(((struct sockaddr_un *)0)->sun_path) + 6;
  char *path_name = gpr_malloc(sizeof(char) * path_name_length);
  memset(path_name, 'a', path_name_length);
  memcpy(path_name, prefix, strlen(prefix) - 1);
  path_name[path_name_length - 1] = '\0';
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resolve_address(&exec_ctx, path_name, NULL,
                       grpc_closure_create(must_fail, &args), &args.addrs);
  gpr_free(path_name);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_executor_init();
  grpc_iomgr_init();
  test_unix_socket();
  test_unix_socket_path_name_too_long();
  return 0;
}
