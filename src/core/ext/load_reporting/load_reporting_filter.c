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

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <string.h>

#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/transport/static_metadata.h"

int grpc_load_reporting_trace = 0;

typedef struct call_data {
  /* an id unique to the call */
  intptr_t id;
  char *trailing_md_string;
  gpr_slice initial_md; /* TODO(dgq): turn into a "token" type once defined */

  /* stores the recv_initial_metadata op's ready closure, which we wrap with our
   * own (on_initial_md_ready) in order to capture the incoming initial metadata
   * */
  grpc_closure *ops_recv_initial_metadata_ready;
  /* copy of the op's initial metadata. We need it because we wrap its callback
   * (see \a ops_recv_initial_metadata_ready). */
  grpc_metadata_batch *recv_initial_metadata;

  /* to get notified of the availability of the incoming initial metadata. */
  grpc_closure on_initial_md_ready;

  /* corresponds to the :path header. */
  const char *service_method;

  /* which backend host the client thinks it's talking to. This may be different
   * from the actual backend in the case of, for example, load-balanced targets
   * */
  const char *target_host;

} call_data;

typedef struct channel_data {
  /* an id unique to the channel */
  intptr_t id;

  /* peer's authenticated identity if available. NULL otherwise */
  char *peer_identity;
} channel_data;

typedef struct {
  grpc_call_element *elem;
  grpc_exec_ctx *exec_ctx;
} recv_md_filter_args;

static grpc_mdelem *recv_md_filter(void *user_data, grpc_mdelem *md) {
  recv_md_filter_args *a = user_data;
  grpc_call_element *elem = a->elem;
  call_data *calld = elem->call_data;

  if (md->key == GRPC_MDSTR_PATH) {
    calld->service_method = grpc_mdstr_as_c_string(md->value);
    if (grpc_load_reporting_trace)
      gpr_log(GPR_DEBUG, "[LR] Service method: '%s'", calld->service_method);
  } else if (calld->target_host == NULL && md->key == GRPC_MDSTR_AUTHORITY) {
    /* the target host is constant per channel */
    calld->target_host = grpc_mdstr_as_c_string(md->value);
    if (grpc_load_reporting_trace)
      gpr_log(GPR_DEBUG, "[LR] Target host: '%s'", calld->target_host);
  } else if (md->key == GRPC_MDSTR_LOAD_REPORTING_INITIAL) {
    calld->initial_md = gpr_slice_ref(md->value->slice);
    if (grpc_load_reporting_trace)
      gpr_log(GPR_DEBUG, "[LR] Initial MD size: '%ld'",
              GPR_SLICE_LENGTH(calld->initial_md));
    return NULL;
  }

  return md;
}

static void on_initial_md_ready(grpc_exec_ctx *exec_ctx, void *user_data,
                                grpc_error *err) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;

  if (err == GRPC_ERROR_NONE) {
    recv_md_filter_args a;
    a.elem = elem;
    a.exec_ctx = exec_ctx;
    grpc_metadata_batch_filter(calld->recv_initial_metadata, recv_md_filter,
                               &a);
  } else {
    GRPC_ERROR_REF(err);
  }
  calld->ops_recv_initial_metadata_ready->cb(
      exec_ctx, calld->ops_recv_initial_metadata_ready->cb_arg, err);
  GRPC_ERROR_UNREF(err);
}

/* Constructor for call_data */
static grpc_error *init_call_elem(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  grpc_call_element_args *args) {
  call_data *calld = elem->call_data;
  memset(calld, 0, sizeof(call_data));

  calld->id = (intptr_t)args->call_stack;
  grpc_closure_init(&calld->on_initial_md_ready, on_initial_md_ready, elem);
  return GRPC_ERROR_NONE;
}

/* Destructor for call_data */
static void destroy_call_elem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              void *ignored) {
  call_data *calld = elem->call_data;

  gpr_slice_unref(calld->initial_md);
  gpr_free(calld->trailing_md_string);
}

/* Constructor for channel_data */
static void init_channel_elem(grpc_exec_ctx *exec_ctx,
                              grpc_channel_element *elem,
                              grpc_channel_element_args *args) {
  GPR_ASSERT(!args->is_last);
  channel_data *chand = elem->channel_data;
  memset(chand, 0, sizeof(channel_data));

  const grpc_auth_context *auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context != NULL &&
      grpc_auth_context_peer_is_authenticated(auth_context)) {
    grpc_auth_property_iterator auth_it =
        grpc_auth_context_peer_identity(auth_context);
    const grpc_auth_property *auth_property =
        grpc_auth_property_iterator_next(&auth_it);
    if (auth_property != NULL) {
      /* TODO(dgq): perhaps we can borrow the value ptr, saving an alloc */
      chand->peer_identity = gpr_strdup(auth_property->value);
    }
  }

  if (grpc_load_reporting_trace) {
    gpr_log(GPR_DEBUG, "[LR] Authenticated user: %s",
            chand->peer_identity ? chand->peer_identity : "<n/a>");
  }

  chand->id = (intptr_t)args->channel_stack;
}

/* Destructor for channel data */
static void destroy_channel_elem(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {
  channel_data *chand = elem->channel_data;
  gpr_free(chand->peer_identity);
}

static grpc_mdelem *lr_trailing_md_filter(void *user_data, grpc_mdelem *md) {
  grpc_call_element *elem = user_data;
  call_data *calld = elem->call_data;

  if (md->key == GRPC_MDSTR_LOAD_REPORTING_TRAILING) {
    calld->trailing_md_string = gpr_strdup(grpc_mdstr_as_c_string(md->value));
    return NULL;
  }

  return md;
}

static void lr_start_transport_stream_op(grpc_exec_ctx *exec_ctx,
                                         grpc_call_element *elem,
                                         grpc_transport_stream_op *op) {
  GPR_TIMER_BEGIN("lr_start_transport_stream_op", 0);
  call_data *calld = elem->call_data;

  if (op->recv_initial_metadata) {
    calld->recv_initial_metadata = op->recv_initial_metadata;
    /* substitute our callback for the higher callback */
    calld->ops_recv_initial_metadata_ready = op->recv_initial_metadata_ready;
    op->recv_initial_metadata_ready = &calld->on_initial_md_ready;
  } else if (op->send_trailing_metadata) {
    grpc_metadata_batch_filter(op->send_trailing_metadata,
                               lr_trailing_md_filter, elem);
  }
  grpc_call_next_op(exec_ctx, elem, op);

  GPR_TIMER_END("lr_start_transport_stream_op", 0);
}

const grpc_channel_filter grpc_load_reporting_filter = {
    lr_start_transport_stream_op,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_call_next_get_peer,
    "load_reporting"};
