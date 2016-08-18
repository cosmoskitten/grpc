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

#include <errno.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"

#define MAX_TABLE_RESIZE 256
#define DEFAULT_TABLE_SIZE 16

#define FD_TO_IDX(fd) (-(fd) - 1)
#define IDX_TO_FD(idx) (-(idx) - 1)

typedef struct cv_node {
  gpr_cv* cv;
  struct cv_node* next;
} cv_node;

typedef struct fd_node {
  int is_set;
  cv_node* cvs;
  struct fd_node* next_free;
} fd_node;

typedef struct cv_fd_table {
  fd_node* cvfds;
  fd_node* free_fds;
  unsigned int size;
  grpc_poll_function_type poll;
} cv_fd_table;

typedef struct poll_args {
  struct pollfd *fds;
  nfds_t nfds;
  int timeout;
  gpr_cv* cv;
  int result;
  int err;
} poll_args;

static gpr_mu g_mu;
static cv_fd_table g_cvfds;

static void run_poll(void *arg) {
  poll_args* pargs = (poll_args*)arg;
  pargs->result = 0; //The result if poll() is interrupted
  pargs->result = g_cvfds.poll(pargs->fds, pargs->nfds, pargs->timeout);
  pargs->err = errno;
  gpr_mu_lock(&g_mu);
  gpr_cv_signal(pargs->cv);
  gpr_mu_unlock(&g_mu);
}

int cvfd_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  unsigned int i;
  int res, idx;
  cv_node *cvn, *prev;
  struct pollfd *sockfds;
  nfds_t nsockfds = 0;
  gpr_cv pollcv;
  gpr_thd_id t_id;
  gpr_thd_options opt;
  poll_args pargs;
  gpr_mu_lock(&g_mu);
  gpr_cv_init(&pollcv);
  for(i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if(fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      idx = FD_TO_IDX(fds[i].fd);
      cvn = gpr_malloc(sizeof(cv_node));
      cvn->cv = &pollcv;
      cvn->next = g_cvfds.cvfds[idx].cvs;
      g_cvfds.cvfds[idx].cvs = cvn;
      // We should return immediately if there are pending events,
      // but we still need to call poll() to check for socket events
      if(g_cvfds.cvfds[idx].is_set) {
        timeout = 0;
      }
    } else if(fds[i].fd >= 0) {
      nsockfds++;
    }
  }
  sockfds = gpr_malloc(sizeof(struct pollfd) * nsockfds);
  idx = 0;
  for(i = 0; i < nfds; i++) {
    if(fds[i].fd >= 0) {
      sockfds[idx].fd = fds[i].fd;
      sockfds[idx].events = fds[i].events;
      sockfds[idx].revents = 0;
      idx++;
    }
  }
  if (nsockfds > 0) {
    pargs.fds = sockfds;
    pargs.nfds = nsockfds;
    pargs.timeout = timeout;
    pargs.cv = &pollcv;

    opt = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&opt);
    gpr_thd_new(&t_id, &run_poll, &pargs, &opt);
    //We want the poll() thread to trigger the deadline, so wait forever here
    gpr_cv_wait(&pollcv, &g_mu, gpr_inf_future(GPR_CLOCK_MONOTONIC));
    gpr_thd_cancel(t_id);

    //The lock may be needed by the thread because the only cancellation
    //point is the poll() call
    gpr_mu_unlock(&g_mu);
    gpr_thd_join(t_id);
    gpr_mu_lock(&g_mu);

    res = pargs.result;
    if(res == -1) {
      errno = pargs.err;
    }
  } else {
    gpr_timespec deadline = gpr_now(GPR_CLOCK_REALTIME);
    deadline = gpr_time_add(deadline, gpr_time_from_millis(timeout, GPR_TIMESPAN));
    gpr_cv_wait(&pollcv, &g_mu, deadline);
  }

  idx = 0;
  for(i = 0; i < nfds; i++) {
    if(fds[i].fd < 0 && (fds[i].events & POLLIN)) {
      cvn = g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].cvs;
      prev = NULL;
      while(cvn->cv != &pollcv) {
        prev = cvn;
        cvn = cvn->next;
        GPR_ASSERT(cvn);
      }
      if(!prev) {
        g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].cvs = cvn->next;
      } else {
        prev->next = cvn->next;
      }
      gpr_free(cvn);

      if(g_cvfds.cvfds[FD_TO_IDX(fds[i].fd)].is_set) {
        fds[i].revents = POLLIN;
        if(res >= 0)
          res++;
      }
    } else if (fds[i].fd>=0) {
      fds[i].revents = sockfds[idx].revents;
      idx++;
    }
  }
  gpr_free(sockfds);
  gpr_cv_destroy(&pollcv);
  gpr_mu_unlock(&g_mu);

  return res;
}

static grpc_error* cv_fd_init(grpc_wakeup_fd* fd_info) {
  unsigned int i, newsize;
  int idx;
  gpr_mu_lock(&g_mu);
  if(!g_cvfds.free_fds) {
    newsize = GPR_MIN(g_cvfds.size * 2, g_cvfds.size + MAX_TABLE_RESIZE);
    g_cvfds.cvfds = gpr_realloc(g_cvfds.cvfds, sizeof(fd_node) * newsize);
    for(i = g_cvfds.size; i < newsize; i++) {
      g_cvfds.cvfds[i].is_set = 0;
      g_cvfds.cvfds[i].cvs = NULL;
      g_cvfds.cvfds[i].next_free = g_cvfds.free_fds;
      g_cvfds.free_fds = &g_cvfds.cvfds[i];
    }
    g_cvfds.size = newsize;
  }

  idx = (int) (g_cvfds.free_fds - g_cvfds.cvfds);
  g_cvfds.free_fds = g_cvfds.free_fds->next_free;
  g_cvfds.cvfds[idx].cvs = NULL;
  g_cvfds.cvfds[idx].is_set = 0;
  fd_info->read_fd = IDX_TO_FD(idx);
  fd_info->write_fd = -1;
  gpr_mu_unlock(&g_mu);
  return GRPC_ERROR_NONE;
}

void grpc_global_cv_fd_table_init() {
  gpr_mu_init(&g_mu);
  g_cvfds.size = DEFAULT_TABLE_SIZE;
  g_cvfds.cvfds = gpr_malloc(sizeof(fd_node) * DEFAULT_TABLE_SIZE);
  g_cvfds.free_fds = NULL;
  for(int i = 0; i < DEFAULT_TABLE_SIZE; i++) {
    g_cvfds.cvfds[i].is_set = 0;
    g_cvfds.cvfds[i].cvs = NULL;
    g_cvfds.cvfds[i].next_free = g_cvfds.free_fds;
    g_cvfds.free_fds = &g_cvfds.cvfds[i];
  }
  //Override the poll function with one that supports cvfds
  g_cvfds.poll = grpc_poll_function;
  grpc_poll_function = &cvfd_poll;
}

void grpc_global_cv_fd_table_shutdown() {
  gpr_mu_destroy(&g_mu);
  gpr_free(g_cvfds.cvfds);
  grpc_poll_function = g_cvfds.poll;
}

static grpc_error* cv_fd_wakeup(grpc_wakeup_fd* fd_info) {
  cv_node* cvn;
  gpr_mu_lock(&g_mu);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].is_set = 1;
  cvn = g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].cvs;
  while(cvn) {
    gpr_cv_signal(cvn->cv);
    cvn = cvn->next;
  }
  gpr_mu_unlock(&g_mu);
  return GRPC_ERROR_NONE;
}

static grpc_error* cv_fd_consume(grpc_wakeup_fd* fd_info) {
  gpr_mu_lock(&g_mu);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].is_set = 0;
  gpr_mu_unlock(&g_mu);
  return GRPC_ERROR_NONE;
}

static void cv_fd_destroy(grpc_wakeup_fd* fd_info) {
  if(fd_info->read_fd == 0) {
    return;
  }
  gpr_mu_lock(&g_mu);
  // Assert that there are no active pollers
  GPR_ASSERT(!g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].cvs);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].next_free = g_cvfds.free_fds;
  g_cvfds.free_fds = &g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)];
  gpr_mu_unlock(&g_mu);
}

static int cv_check_availability(void) {
  return 1;
}

const grpc_wakeup_fd_vtable grpc_cv_wakeup_fd_vtable = {
    cv_fd_init, cv_fd_consume, cv_fd_wakeup, cv_fd_destroy,
    cv_check_availability};
