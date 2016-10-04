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

#include "src/core/lib/iomgr/buffer_pool.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/combiner.h"

int grpc_buffer_pool_trace = 0;

typedef bool (*bpstate_func)(grpc_exec_ctx *exec_ctx,
                             grpc_buffer_pool *buffer_pool);

typedef struct {
  grpc_buffer_user *head;
  grpc_buffer_user *tail;
} grpc_buffer_user_list;

struct grpc_buffer_pool {
  gpr_refcount refs;

  grpc_combiner *combiner;
  int64_t size;
  int64_t free_pool;

  bool step_scheduled;
  bool reclaiming;
  grpc_closure bpstep_closure;
  grpc_closure bpreclaimation_done_closure;

  grpc_buffer_user *roots[GRPC_BULIST_COUNT];

  char *name;
};

/*******************************************************************************
 * list management
 */

static void bulist_add_tail(grpc_buffer_user *buffer_user, grpc_bulist list) {
  grpc_buffer_pool *buffer_pool = buffer_user->buffer_pool;
  grpc_buffer_user **root = &buffer_pool->roots[list];
  if (*root == NULL) {
    *root = buffer_user;
    buffer_user->links[list].next = buffer_user->links[list].prev = buffer_user;
  } else {
    buffer_user->links[list].next = *root;
    buffer_user->links[list].prev = (*root)->links[list].prev;
    buffer_user->links[list].next->links[list].prev =
        buffer_user->links[list].prev->links[list].next = buffer_user;
  }
}

static void bulist_add_head(grpc_buffer_user *buffer_user, grpc_bulist list) {
  grpc_buffer_pool *buffer_pool = buffer_user->buffer_pool;
  grpc_buffer_user **root = &buffer_pool->roots[list];
  if (*root == NULL) {
    *root = buffer_user;
    buffer_user->links[list].next = buffer_user->links[list].prev = buffer_user;
  } else {
    buffer_user->links[list].next = (*root)->links[list].next;
    buffer_user->links[list].prev = *root;
    buffer_user->links[list].next->links[list].prev =
        buffer_user->links[list].prev->links[list].next = buffer_user;
    *root = buffer_user;
  }
}

static bool bulist_empty(grpc_buffer_pool *buffer_pool, grpc_bulist list) {
  return buffer_pool->roots[list] == NULL;
}

static grpc_buffer_user *bulist_pop(grpc_buffer_pool *buffer_pool,
                                    grpc_bulist list) {
  grpc_buffer_user **root = &buffer_pool->roots[list];
  grpc_buffer_user *buffer_user = *root;
  if (buffer_user == NULL) {
    return NULL;
  }
  if (buffer_user->links[list].next == buffer_user) {
    *root = NULL;
  } else {
    buffer_user->links[list].next->links[list].prev =
        buffer_user->links[list].prev;
    buffer_user->links[list].prev->links[list].next =
        buffer_user->links[list].next;
    *root = buffer_user->links[list].next;
  }
  buffer_user->links[list].next = buffer_user->links[list].prev = NULL;
  return buffer_user;
}

static void bulist_remove(grpc_buffer_user *buffer_user, grpc_bulist list) {
  if (buffer_user->links[list].next == NULL) return;
  grpc_buffer_pool *buffer_pool = buffer_user->buffer_pool;
  if (buffer_pool->roots[list] == buffer_user) {
    buffer_pool->roots[list] = buffer_user->links[list].next;
    if (buffer_pool->roots[list] == buffer_user) {
      buffer_pool->roots[list] = NULL;
    }
  }
  buffer_user->links[list].next->links[list].prev =
      buffer_user->links[list].prev;
  buffer_user->links[list].prev->links[list].next =
      buffer_user->links[list].next;
}

/*******************************************************************************
 * buffer pool state machine
 */

static bool bpalloc(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool);
static bool bpscavenge(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool);
static bool bpreclaim(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool,
                      bool destructive);

static void bpstep(grpc_exec_ctx *exec_ctx, void *bp, grpc_error *error) {
  grpc_buffer_pool *buffer_pool = bp;
  buffer_pool->step_scheduled = false;
  do {
    if (bpalloc(exec_ctx, buffer_pool)) goto done;
  } while (bpscavenge(exec_ctx, buffer_pool));
  bpreclaim(exec_ctx, buffer_pool, false) ||
      bpreclaim(exec_ctx, buffer_pool, true);
done:
  grpc_buffer_pool_internal_unref(exec_ctx, buffer_pool);
}

static void bpstep_sched(grpc_exec_ctx *exec_ctx,
                         grpc_buffer_pool *buffer_pool) {
  if (buffer_pool->step_scheduled) return;
  buffer_pool->step_scheduled = true;
  grpc_buffer_pool_internal_ref(buffer_pool);
  grpc_combiner_execute_finally(exec_ctx, buffer_pool->combiner,
                                &buffer_pool->bpstep_closure, GRPC_ERROR_NONE,
                                false);
}

/* returns true if all allocations are completed */
static bool bpalloc(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool) {
  grpc_buffer_user *buffer_user;
  while ((buffer_user =
              bulist_pop(buffer_pool, GRPC_BULIST_AWAITING_ALLOCATION))) {
    gpr_mu_lock(&buffer_user->mu);
    if (buffer_user->free_pool < 0 &&
        -buffer_user->free_pool <= buffer_pool->free_pool) {
      int64_t amt = -buffer_user->free_pool;
      buffer_user->free_pool = 0;
      buffer_pool->free_pool -= amt;
      if (grpc_buffer_pool_trace) {
        gpr_log(GPR_DEBUG, "BP %s %s: grant alloc %" PRId64
                           " bytes; bp_free_pool -> %" PRId64,
                buffer_pool->name, buffer_user->name, amt,
                buffer_pool->free_pool);
      }
    } else if (grpc_buffer_pool_trace && buffer_user->free_pool >= 0) {
      gpr_log(GPR_DEBUG, "BP %s %s: discard already satisfied alloc request",
              buffer_pool->name, buffer_user->name);
    }
    if (buffer_user->free_pool >= 0) {
      buffer_user->allocating = false;
      grpc_exec_ctx_enqueue_list(exec_ctx, &buffer_user->on_allocated, NULL);
      gpr_mu_unlock(&buffer_user->mu);
    } else {
      bulist_add_head(buffer_user, GRPC_BULIST_AWAITING_ALLOCATION);
      gpr_mu_unlock(&buffer_user->mu);
      return false;
    }
  }
  return true;
}

/* returns true if any memory could be reclaimed from buffers */
static bool bpscavenge(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool) {
  grpc_buffer_user *buffer_user;
  while ((buffer_user =
              bulist_pop(buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL))) {
    gpr_mu_lock(&buffer_user->mu);
    if (buffer_user->free_pool > 0) {
      int64_t amt = buffer_user->free_pool;
      buffer_user->free_pool = 0;
      buffer_pool->free_pool += amt;
      if (grpc_buffer_pool_trace) {
        gpr_log(GPR_DEBUG, "BP %s %s: scavenge %" PRId64
                           " bytes; bp_free_pool -> %" PRId64,
                buffer_pool->name, buffer_user->name, amt,
                buffer_pool->free_pool);
      }
      gpr_mu_unlock(&buffer_user->mu);
      return true;
    } else {
      gpr_mu_unlock(&buffer_user->mu);
    }
  }
  return false;
}

/* returns true if reclaimation is proceeding */
static bool bpreclaim(grpc_exec_ctx *exec_ctx, grpc_buffer_pool *buffer_pool,
                      bool destructive) {
  if (buffer_pool->reclaiming) return true;
  grpc_bulist list = destructive ? GRPC_BULIST_RECLAIMER_DESTRUCTIVE
                                 : GRPC_BULIST_RECLAIMER_BENIGN;
  grpc_buffer_user *buffer_user = bulist_pop(buffer_pool, list);
  if (buffer_user == NULL) return false;
  if (grpc_buffer_pool_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: initiate %s reclaimation", buffer_pool->name,
            buffer_user->name, destructive ? "destructive" : "benign");
  }
  buffer_pool->reclaiming = true;
  grpc_buffer_pool_internal_ref(buffer_pool);
  grpc_closure *c = buffer_user->reclaimers[destructive];
  buffer_user->reclaimers[destructive] = NULL;
  grpc_closure_run(exec_ctx, c, GRPC_ERROR_NONE);
  return true;
}

/*******************************************************************************
 * bu_slice: a slice implementation that is backed by a grpc_buffer_user
 */

typedef struct {
  gpr_slice_refcount base;
  gpr_refcount refs;
  grpc_buffer_user *buffer_user;
  size_t size;
} bu_slice_refcount;

static void bu_slice_ref(void *p) {
  bu_slice_refcount *rc = p;
  gpr_ref(&rc->refs);
}

static void bu_slice_unref(void *p) {
  bu_slice_refcount *rc = p;
  if (gpr_unref(&rc->refs)) {
    /* TODO(ctiller): this is dangerous, but I think safe for now:
       we have no guarantee here that we're at a safe point for creating an
       execution context, but we have no way of writing this code otherwise.
       In the future: consider lifting gpr_slice to grpc, and offering an
       internal_{ref,unref} pair that is execution context aware. Alternatively,
       make exec_ctx be thread local and 'do the right thing' (whatever that is)
       if NULL */
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_buffer_user_free(&exec_ctx, rc->buffer_user, rc->size);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_free(rc);
  }
}

static gpr_slice bu_slice_create(grpc_buffer_user *buffer_user, size_t size) {
  bu_slice_refcount *rc = gpr_malloc(sizeof(bu_slice_refcount) + size);
  rc->base.ref = bu_slice_ref;
  rc->base.unref = bu_slice_unref;
  gpr_ref_init(&rc->refs, 1);
  rc->buffer_user = buffer_user;
  rc->size = size;
  gpr_slice slice;
  slice.refcount = &rc->base;
  slice.data.refcounted.bytes = (uint8_t *)(rc + 1);
  slice.data.refcounted.length = size;
  return slice;
}

/*******************************************************************************
 * grpc_buffer_pool internal implementation
 */

static void bu_allocate(grpc_exec_ctx *exec_ctx, void *bu, grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_AWAITING_ALLOCATION)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add_tail(buffer_user, GRPC_BULIST_AWAITING_ALLOCATION);
}

static void bu_add_to_free_pool(grpc_exec_ctx *exec_ctx, void *bu,
                                grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add_tail(buffer_user, GRPC_BULIST_NON_EMPTY_FREE_POOL);
}

static void bu_post_benign_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                     grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_RECLAIMER_BENIGN)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add_tail(buffer_user, GRPC_BULIST_RECLAIMER_BENIGN);
}

static void bu_post_destructive_reclaimer(grpc_exec_ctx *exec_ctx, void *bu,
                                          grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  if (!bulist_empty(buffer_user->buffer_pool,
                    GRPC_BULIST_AWAITING_ALLOCATION) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_NON_EMPTY_FREE_POOL) &&
      bulist_empty(buffer_user->buffer_pool, GRPC_BULIST_RECLAIMER_BENIGN) &&
      bulist_empty(buffer_user->buffer_pool,
                   GRPC_BULIST_RECLAIMER_DESTRUCTIVE)) {
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
  bulist_add_tail(buffer_user, GRPC_BULIST_RECLAIMER_DESTRUCTIVE);
}

static void bu_destroy(grpc_exec_ctx *exec_ctx, void *bu, grpc_error *error) {
  grpc_buffer_user *buffer_user = bu;
  GPR_ASSERT(buffer_user->allocated == 0);
  for (int i = 0; i < GRPC_BULIST_COUNT; i++) {
    bulist_remove(buffer_user, (grpc_bulist)i);
  }
  grpc_exec_ctx_sched(exec_ctx, buffer_user->reclaimers[0],
                      GRPC_ERROR_CANCELLED, NULL);
  grpc_exec_ctx_sched(exec_ctx, buffer_user->reclaimers[1],
                      GRPC_ERROR_CANCELLED, NULL);
  grpc_exec_ctx_sched(exec_ctx, (grpc_closure *)gpr_atm_no_barrier_load(
                                    &buffer_user->on_done_destroy_closure),
                      GRPC_ERROR_NONE, NULL);
  if (buffer_user->free_pool != 0) {
    buffer_user->buffer_pool->free_pool += buffer_user->free_pool;
    bpstep_sched(exec_ctx, buffer_user->buffer_pool);
  }
}

static void bu_allocated_slices(grpc_exec_ctx *exec_ctx, void *ts,
                                grpc_error *error) {
  grpc_buffer_user_slice_allocator *slice_allocator = ts;
  if (error == GRPC_ERROR_NONE) {
    for (size_t i = 0; i < slice_allocator->count; i++) {
      gpr_slice_buffer_add_indexed(slice_allocator->dest,
                                   bu_slice_create(slice_allocator->buffer_user,
                                                   slice_allocator->length));
    }
  }
  grpc_closure_run(exec_ctx, &slice_allocator->on_done, GRPC_ERROR_REF(error));
}

typedef struct {
  int64_t size;
  grpc_buffer_pool *buffer_pool;
  grpc_closure closure;
} bp_resize_args;

static void bp_resize(grpc_exec_ctx *exec_ctx, void *args, grpc_error *error) {
  bp_resize_args *a = args;
  int64_t delta = a->size - a->buffer_pool->size;
  a->buffer_pool->size += delta;
  a->buffer_pool->free_pool += delta;
  if (delta < 0 && a->buffer_pool->free_pool < 0) {
    bpstep_sched(exec_ctx, a->buffer_pool);
  } else if (delta > 0 &&
             !bulist_empty(a->buffer_pool, GRPC_BULIST_AWAITING_ALLOCATION)) {
    bpstep_sched(exec_ctx, a->buffer_pool);
  }
  grpc_buffer_pool_internal_unref(exec_ctx, a->buffer_pool);
  gpr_free(a);
}

static void bp_reclaimation_done(grpc_exec_ctx *exec_ctx, void *bp,
                                 grpc_error *error) {
  grpc_buffer_pool *buffer_pool = bp;
  buffer_pool->reclaiming = false;
  bpstep_sched(exec_ctx, buffer_pool);
  grpc_buffer_pool_internal_unref(exec_ctx, buffer_pool);
}

/*******************************************************************************
 * grpc_buffer_pool api
 */

grpc_buffer_pool *grpc_buffer_pool_create(const char *name) {
  grpc_buffer_pool *buffer_pool = gpr_malloc(sizeof(*buffer_pool));
  gpr_ref_init(&buffer_pool->refs, 1);
  buffer_pool->combiner = grpc_combiner_create(NULL);
  buffer_pool->free_pool = INT64_MAX;
  buffer_pool->size = INT64_MAX;
  buffer_pool->step_scheduled = false;
  buffer_pool->reclaiming = false;
  if (name != NULL) {
    buffer_pool->name = gpr_strdup(name);
  } else {
    gpr_asprintf(&buffer_pool->name, "anonymous_pool_%" PRIxPTR,
                 (intptr_t)buffer_pool);
  }
  grpc_closure_init(&buffer_pool->bpstep_closure, bpstep, buffer_pool);
  grpc_closure_init(&buffer_pool->bpreclaimation_done_closure,
                    bp_reclaimation_done, buffer_pool);
  for (int i = 0; i < GRPC_BULIST_COUNT; i++) {
    buffer_pool->roots[i] = NULL;
  }
  return buffer_pool;
}

void grpc_buffer_pool_internal_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_buffer_pool *buffer_pool) {
  if (gpr_unref(&buffer_pool->refs)) {
    grpc_combiner_destroy(exec_ctx, buffer_pool->combiner);
    gpr_free(buffer_pool->name);
    gpr_free(buffer_pool);
  }
}

void grpc_buffer_pool_unref(grpc_buffer_pool *buffer_pool) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_buffer_pool_internal_unref(&exec_ctx, buffer_pool);
  grpc_exec_ctx_finish(&exec_ctx);
}

grpc_buffer_pool *grpc_buffer_pool_internal_ref(grpc_buffer_pool *buffer_pool) {
  gpr_ref(&buffer_pool->refs);
  return buffer_pool;
}

void grpc_buffer_pool_ref(grpc_buffer_pool *buffer_pool) {
  grpc_buffer_pool_internal_ref(buffer_pool);
}

void grpc_buffer_pool_resize(grpc_buffer_pool *buffer_pool, size_t size) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  bp_resize_args *a = gpr_malloc(sizeof(*a));
  a->buffer_pool = grpc_buffer_pool_internal_ref(buffer_pool);
  a->size = (int64_t)size;
  grpc_closure_init(&a->closure, bp_resize, a);
  grpc_combiner_execute(&exec_ctx, buffer_pool->combiner, &a->closure,
                        GRPC_ERROR_NONE, false);
  grpc_exec_ctx_finish(&exec_ctx);
}

/*******************************************************************************
 * grpc_buffer_user channel args api
 */

grpc_buffer_pool *grpc_buffer_pool_from_channel_args(
    const grpc_channel_args *channel_args) {
  for (size_t i = 0; i < channel_args->num_args; i++) {
    if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_BUFFER_POOL)) {
      if (channel_args->args[i].type == GRPC_ARG_POINTER) {
        return grpc_buffer_pool_internal_ref(
            channel_args->args[i].value.pointer.p);
      } else {
        gpr_log(GPR_DEBUG, GRPC_ARG_BUFFER_POOL " should be a pointer");
      }
    }
  }
  return grpc_buffer_pool_create(NULL);
}

static void *bp_copy(void *bp) {
  grpc_buffer_pool_ref(bp);
  return bp;
}

static void bp_destroy(void *bp) { grpc_buffer_pool_unref(bp); }

static int bp_cmp(void *a, void *b) { return GPR_ICMP(a, b); }

const grpc_arg_pointer_vtable *grpc_buffer_pool_arg_vtable(void) {
  static const grpc_arg_pointer_vtable vtable = {bp_copy, bp_destroy, bp_cmp};
  return &vtable;
}

/*******************************************************************************
 * grpc_buffer_user api
 */

void grpc_buffer_user_init(grpc_buffer_user *buffer_user,
                           grpc_buffer_pool *buffer_pool, const char *name) {
  buffer_user->buffer_pool = grpc_buffer_pool_internal_ref(buffer_pool);
  grpc_closure_init(&buffer_user->allocate_closure, &bu_allocate, buffer_user);
  grpc_closure_init(&buffer_user->add_to_free_pool_closure,
                    &bu_add_to_free_pool, buffer_user);
  grpc_closure_init(&buffer_user->post_reclaimer_closure[0],
                    &bu_post_benign_reclaimer, buffer_user);
  grpc_closure_init(&buffer_user->post_reclaimer_closure[1],
                    &bu_post_destructive_reclaimer, buffer_user);
  grpc_closure_init(&buffer_user->destroy_closure, &bu_destroy, buffer_user);
  gpr_mu_init(&buffer_user->mu);
  buffer_user->allocated = 0;
  buffer_user->free_pool = 0;
  grpc_closure_list_init(&buffer_user->on_allocated);
  buffer_user->allocating = false;
  buffer_user->added_to_free_pool = false;
  gpr_atm_no_barrier_store(&buffer_user->on_done_destroy_closure, 0);
  buffer_user->reclaimers[0] = NULL;
  buffer_user->reclaimers[1] = NULL;
  for (int i = 0; i < GRPC_BULIST_COUNT; i++) {
    buffer_user->links[i].next = buffer_user->links[i].prev = NULL;
  }
#ifndef NDEBUG
  buffer_user->asan_canary = gpr_malloc(1);
#endif
  if (name != NULL) {
    buffer_user->name = gpr_strdup(name);
  } else {
    gpr_asprintf(&buffer_user->name, "anonymous_buffer_user_%" PRIxPTR,
                 (intptr_t)buffer_user);
  }
}

void grpc_buffer_user_shutdown(grpc_exec_ctx *exec_ctx,
                               grpc_buffer_user *buffer_user,
                               grpc_closure *on_done) {
  gpr_mu_lock(&buffer_user->mu);
  GPR_ASSERT(gpr_atm_no_barrier_load(&buffer_user->on_done_destroy_closure) ==
             0);
  gpr_atm_no_barrier_store(&buffer_user->on_done_destroy_closure,
                           (gpr_atm)on_done);
  if (buffer_user->allocated == 0) {
    grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                          &buffer_user->destroy_closure, GRPC_ERROR_NONE,
                          false);
  }
  gpr_mu_unlock(&buffer_user->mu);
}

void grpc_buffer_user_destroy(grpc_exec_ctx *exec_ctx,
                              grpc_buffer_user *buffer_user) {
#ifndef NDEBUG
  gpr_free(buffer_user->asan_canary);
#endif
  grpc_buffer_pool_internal_unref(exec_ctx, buffer_user->buffer_pool);
  gpr_mu_destroy(&buffer_user->mu);
  gpr_free(buffer_user->name);
}

void grpc_buffer_user_alloc(grpc_exec_ctx *exec_ctx,
                            grpc_buffer_user *buffer_user, size_t size,
                            grpc_closure *optional_on_done) {
  gpr_mu_lock(&buffer_user->mu);
  grpc_closure *on_done_destroy = (grpc_closure *)gpr_atm_no_barrier_load(
      &buffer_user->on_done_destroy_closure);
  if (on_done_destroy != NULL) {
    /* already shutdown */
    if (grpc_buffer_pool_trace) {
      gpr_log(GPR_DEBUG, "BP %s %s: alloc %" PRIdPTR " after shutdown",
              buffer_user->buffer_pool->name, buffer_user->name, size);
    }
    grpc_exec_ctx_sched(
        exec_ctx, optional_on_done,
        GRPC_ERROR_CREATE("Buffer pool user is already shutdown"), NULL);
    gpr_mu_unlock(&buffer_user->mu);
    return;
  }
  buffer_user->allocated += (int64_t)size;
  buffer_user->free_pool -= (int64_t)size;
  if (grpc_buffer_pool_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: alloc %" PRIdPTR "; allocated -> %" PRId64
                       ", free_pool -> %" PRId64,
            buffer_user->buffer_pool->name, buffer_user->name, size,
            buffer_user->allocated, buffer_user->free_pool);
  }
  if (buffer_user->free_pool < 0) {
    grpc_closure_list_append(&buffer_user->on_allocated, optional_on_done,
                             GRPC_ERROR_NONE);
    if (!buffer_user->allocating) {
      buffer_user->allocating = true;
      grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                            &buffer_user->allocate_closure, GRPC_ERROR_NONE,
                            false);
    }
  } else {
    grpc_exec_ctx_sched(exec_ctx, optional_on_done, GRPC_ERROR_NONE, NULL);
  }
  gpr_mu_unlock(&buffer_user->mu);
}

void grpc_buffer_user_free(grpc_exec_ctx *exec_ctx,
                           grpc_buffer_user *buffer_user, size_t size) {
  gpr_mu_lock(&buffer_user->mu);
  GPR_ASSERT(buffer_user->allocated >= (int64_t)size);
  bool was_zero_or_negative = buffer_user->free_pool <= 0;
  buffer_user->free_pool += (int64_t)size;
  buffer_user->allocated -= (int64_t)size;
  if (grpc_buffer_pool_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: free %" PRIdPTR "; allocated -> %" PRId64
                       ", free_pool -> %" PRId64,
            buffer_user->buffer_pool->name, buffer_user->name, size,
            buffer_user->allocated, buffer_user->free_pool);
  }
  bool is_bigger_than_zero = buffer_user->free_pool > 0;
  if (is_bigger_than_zero && was_zero_or_negative &&
      !buffer_user->added_to_free_pool) {
    buffer_user->added_to_free_pool = true;
    grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                          &buffer_user->add_to_free_pool_closure,
                          GRPC_ERROR_NONE, false);
  }
  grpc_closure *on_done_destroy = (grpc_closure *)gpr_atm_no_barrier_load(
      &buffer_user->on_done_destroy_closure);
  if (on_done_destroy != NULL && buffer_user->allocated == 0) {
    grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                          &buffer_user->destroy_closure, GRPC_ERROR_NONE,
                          false);
  }
  gpr_mu_unlock(&buffer_user->mu);
}

void grpc_buffer_user_post_reclaimer(grpc_exec_ctx *exec_ctx,
                                     grpc_buffer_user *buffer_user,
                                     bool destructive, grpc_closure *closure) {
  if (gpr_atm_acq_load(&buffer_user->on_done_destroy_closure) == 0) {
    GPR_ASSERT(buffer_user->reclaimers[destructive] == NULL);
    buffer_user->reclaimers[destructive] = closure;
    grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                          &buffer_user->post_reclaimer_closure[destructive],
                          GRPC_ERROR_NONE, false);
  } else {
    grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_CANCELLED, NULL);
  }
}

void grpc_buffer_user_finish_reclaimation(grpc_exec_ctx *exec_ctx,
                                          grpc_buffer_user *buffer_user) {
  if (grpc_buffer_pool_trace) {
    gpr_log(GPR_DEBUG, "BP %s %s: reclaimation complete",
            buffer_user->buffer_pool->name, buffer_user->name);
  }
  grpc_combiner_execute(exec_ctx, buffer_user->buffer_pool->combiner,
                        &buffer_user->buffer_pool->bpreclaimation_done_closure,
                        GRPC_ERROR_NONE, false);
}

void grpc_buffer_user_slice_allocator_init(
    grpc_buffer_user_slice_allocator *slice_allocator,
    grpc_buffer_user *buffer_user, grpc_iomgr_cb_func cb, void *p) {
  grpc_closure_init(&slice_allocator->on_allocated, bu_allocated_slices,
                    slice_allocator);
  grpc_closure_init(&slice_allocator->on_done, cb, p);
  slice_allocator->buffer_user = buffer_user;
}

void grpc_buffer_user_alloc_slices(
    grpc_exec_ctx *exec_ctx, grpc_buffer_user_slice_allocator *slice_allocator,
    size_t length, size_t count, gpr_slice_buffer *dest) {
  slice_allocator->length = length;
  slice_allocator->count = count;
  slice_allocator->dest = dest;
  grpc_buffer_user_alloc(exec_ctx, slice_allocator->buffer_user, count * length,
                         &slice_allocator->on_allocated);
}