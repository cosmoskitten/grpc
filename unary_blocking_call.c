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

#include "unary_blocking_call.h"
#include <stdio.h>

static void op_send_metadata_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
}

static void op_send_metadata_finish(grpc_context *context, bool *status, int max_message_size) {

}

const grpc_op_manager grpc_op_send_metadata = {
  op_send_metadata_fill,
  op_send_metadata_finish
};

static void op_send_object_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_SEND_MESSAGE;
  gpr_slice slice = gpr_slice_from_copied_buffer(message.data, message.length);
  op->data.send_message = grpc_raw_byte_buffer_create(&slice, 1);
  GPR_ASSERT(op->data.send_message != NULL);
  op->flags = 0;
  op->reserved = NULL;
}

static void op_send_object_finish(grpc_context *context, bool *status, int max_message_size) {

}

const grpc_op_manager grpc_op_send_object = {
  op_send_object_fill,
  op_send_object_finish
};

static void op_recv_metadata_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  grpc_metadata_array_init(&context->recv_metadata_array);
  op->data.recv_initial_metadata = &context->recv_metadata_array;
  op->flags = 0;
  op->reserved = NULL;
}

static void op_recv_metadata_finish(grpc_context *context, bool *status, int max_message_size) {

}

const grpc_op_manager grpc_op_recv_metadata = {
  op_recv_metadata_fill,
  op_recv_metadata_finish
};

static void op_recv_object_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_RECV_MESSAGE;
  context->recv_buffer = NULL;
  op->data.recv_message = &context->recv_buffer;
  op->flags = 0;
  op->reserved = NULL;
}

static void op_recv_object_finish(grpc_context *context, bool *status, int max_message_size) {
}

const grpc_op_manager grpc_op_recv_object = {
  op_recv_object_fill,
  op_recv_object_finish
};

static void op_send_close_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
}

static void op_send_close_finish(grpc_context *context, bool *status, int max_message_size) {
}

const grpc_op_manager grpc_op_send_close = {
  op_send_close_fill,
  op_send_close_finish
};

static void op_recv_status_fill(grpc_op *op, const grpc_method *method, grpc_context *context, const grpc_message message, void *response) {
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  grpc_metadata_array_init(&context->trailing_metadata_array);
  context->status.details = NULL;
  context->status.details_length = 0;

  op->data.recv_status_on_client.trailing_metadata = &context->trailing_metadata_array;
  op->data.recv_status_on_client.status = &context->status.code;
  op->data.recv_status_on_client.status_details = &context->status.details;
  op->data.recv_status_on_client.status_details_capacity = &context->status.details_length;
  op->flags = 0;
  op->reserved = NULL;
}

static void op_recv_status_finish(grpc_context *context, bool *status, int max_message_size) {
}

const grpc_op_manager grpc_op_recv_status = {
  op_recv_status_fill,
  op_recv_status_finish
};

typedef const grpc_op_manager grpc_call_set[GRPC_MAX_OP_COUNT];

void grpc_fill_op_from_call_set(grpc_call_set set, const grpc_method *rpc_method, grpc_context *context,
                                const grpc_message message, void *response, grpc_op ops[], size_t *nops) {
  size_t count = 0;
  while (count < GRPC_MAX_OP_COUNT) {
    if (set[count].fill == NULL && set[count].finish == NULL) break;   // end of call set
    if (set[count].fill == NULL) continue;
    set[count].fill(&ops[count], rpc_method, context, message, response);
    count++;
  }
  *nops = count;
}

void grpc_finish_op_from_call_set(grpc_call_set set, grpc_context *context) {
  size_t count = 0;
  while (count < GRPC_MAX_OP_COUNT) {
    if (set[count].fill == NULL && set[count].finish == NULL) break;   // end of call set
    if (set[count].finish == NULL) continue;
    size_t size = 100;  // ??
    bool status;
    set[count].finish(context, &status, size);
    count++;
  }
}

grpc_status grpc_unary_blocking_call(grpc_channel *channel, const grpc_method *rpc_method, grpc_context *context, const grpc_message message, void *response) {
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  grpc_call *call = grpc_channel_create_call(channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
              "/helloworld.Greeter/SayHello", "0.0.0.0", context->deadline, NULL);

  grpc_call_set set = {
    grpc_op_send_metadata,
    grpc_op_recv_metadata,
    grpc_op_send_object,
    grpc_op_recv_object,
    grpc_op_send_close,
    grpc_op_recv_status
  };

  size_t nops;
  grpc_op ops[GRPC_MAX_OP_COUNT];
  grpc_fill_op_from_call_set(set, rpc_method, context, message, response, ops, &nops);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops, nops, TAG(set), NULL));
  grpc_event ev = grpc_completion_queue_pluck(cq, TAG(set), context->deadline, NULL);

  grpc_finish_op_from_call_set(set, context);

  printf("Status: %d\n", context->status.code);
  printf("Details: %s\n", context->status.details);
  GPR_ASSERT(context->status.code == GRPC_STATUS_OK);

  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, context->recv_buffer);
  gpr_slice slice_recv = grpc_byte_buffer_reader_readall(&reader);
  uint8_t *resp = GPR_SLICE_START_PTR(slice_recv);
  printf("Server said: %s\n", resp + 2);    // skip to the string in serialized protobuf object
  grpc_byte_buffer_destroy(context->recv_buffer);

  grpc_completion_queue_shutdown(cq);
  while (
    grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL)
      .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_completion_queue_destroy(cq);
  grpc_call_destroy(call);

  gpr_free(context->status.details);
}


