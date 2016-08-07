#include <unistd.h>

#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/ev_posix.h"

typedef struct poll_args {
  struct pollfd *fds;
  nfds_t nfds;
  int timeout;
  int result;
} poll_args;

// Mocks posix poll() function
int mock_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  GPR_ASSERT(nfds == 3);
  GPR_ASSERT(fds[0].fd == 20);
  GPR_ASSERT(fds[1].fd == 30);
  GPR_ASSERT(fds[2].fd == 50);
  GPR_ASSERT(fds[0].events == (POLLIN | POLLHUP));
  GPR_ASSERT(fds[1].events == (POLLIN | POLLHUP));
  GPR_ASSERT(fds[2].events == POLLIN);
  if(timeout != 0) {
    sleep(1000);
  }
  return 0;
}

void background_poll(void* args) {
  poll_args* pargs = (poll_args*)args;
  pargs->result = grpc_poll_function(pargs->fds, pargs->nfds, pargs->timeout);
}

void test_many_fds(void) {
  int i;
  grpc_wakeup_fd_global_init();
  grpc_wakeup_fd fd[1000];
  for(i = 0; i < 1000; i++) {
    GPR_ASSERT(grpc_wakeup_fd_init(&fd[i]) == GRPC_ERROR_NONE);
  }
  for(i = 0; i < 1000; i++) {
    grpc_wakeup_fd_destroy(&fd[i]);
  }
  grpc_wakeup_fd_global_destroy();
}

void test_poll_cv_trigger(void) {
  grpc_wakeup_fd cvfd1, cvfd2, cvfd3;
  struct pollfd pfds[6];
  poll_args pargs;
  gpr_thd_id t_id;
  gpr_thd_options opt;
  grpc_poll_function = &mock_poll;
  grpc_wakeup_fd_global_init();

  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd1) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd2) == GRPC_ERROR_NONE);
  GPR_ASSERT(grpc_wakeup_fd_init(&cvfd3) == GRPC_ERROR_NONE);
  GPR_ASSERT(cvfd1.read_fd < 0);
  GPR_ASSERT(cvfd2.read_fd < 0);
  GPR_ASSERT(cvfd3.read_fd < 0);
  GPR_ASSERT(cvfd1.read_fd != cvfd2.read_fd);
  GPR_ASSERT(cvfd2.read_fd != cvfd3.read_fd);
  GPR_ASSERT(cvfd1.read_fd != cvfd3.read_fd);

  pfds[0].fd = cvfd1.read_fd;
  pfds[1].fd = cvfd2.read_fd;
  pfds[2].fd = 20;
  pfds[3].fd = 30;
  pfds[4].fd = cvfd3.read_fd;
  pfds[5].fd = 50;

  pfds[0].events = 0;
  pfds[1].events = POLLIN;
  pfds[2].events = POLLIN | POLLHUP;
  pfds[3].events = POLLIN | POLLHUP;
  pfds[4].events = POLLIN;
  pfds[5].events = POLLIN;

  pargs.fds = pfds;
  pargs.nfds = 6;
  pargs.timeout = 5000;
  pargs.result = 1;

  opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  gpr_thd_new(&t_id, &background_poll, &pargs, &opt);

  // The poll shouldn't complete because pfds[0].events = 0;
  GPR_ASSERT(grpc_wakeup_fd_wakeup(&cvfd1) == GRPC_ERROR_NONE);
  sleep(1);
  GPR_ASSERT(pargs.result == 1);

  GPR_ASSERT(grpc_wakeup_fd_wakeup(&cvfd2) == GRPC_ERROR_NONE);
  gpr_thd_join(t_id);

  GPR_ASSERT(pargs.result == 1);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == POLLIN);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  // Should return immedately, we haven't consumed our event
  grpc_poll_function(pfds, 6, 5000);

  GPR_ASSERT(pargs.result == 1);
  GPR_ASSERT(pfds[0].revents == 0);
  GPR_ASSERT(pfds[1].revents == POLLIN);
  GPR_ASSERT(pfds[2].revents == 0);
  GPR_ASSERT(pfds[3].revents == 0);
  GPR_ASSERT(pfds[4].revents == 0);
  GPR_ASSERT(pfds[5].revents == 0);

  grpc_wakeup_fd_global_destroy();
}

int main(int argc, char **argv) {
  grpc_use_cv_wakeup_fd = 1;
  grpc_allow_specialized_wakeup_fd = 0;
  test_many_fds();
  test_poll_cv_trigger();
  return 0;
}
