#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

DEFINE_bool(daemon, true, "run in daemon mode?");

typedef void (*signal_handler)(int);
static int daemonize();
static int fork_child(pid_t* child_id);
static int master_process_cycle();
static void signal_child(int sig);
static void signal_terminate(int sig);
static void install_signal(int sig, signal_handler handler);
extern int Main();

static pid_t g_child_id = -1;
static bool g_quit = false;

int main(int argc, const char** argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logbuflevel = -1;
  pid_t pid = getpid();
  LOG(INFO) << "start " << argv[0] << " pid: " << pid;
  int rc = 0;
  if (FLAGS_daemon) {
    if (daemonize() != 0) {
      LOG(FATAL) << "daemonize failed";
      exit(-1);
    }
    LOG(INFO) << "daemonize success";
    if (fork_child(&g_child_id) != 0) {
      LOG(FATAL) << "fork child failed";
      exit(-1);
    }
    LOG(INFO) << "fork child success";
    return master_process_cycle();
  }
  return Main();
}

int daemonize() {
  LOG(INFO) << "daemonize";
  pid_t pid = fork();
  switch (pid) {
    case -1:
      LOG(ERROR) << "fork failed: " << strerror(errno);;
      return -1;
    case 0:
      // child
      break;
    default:
      // parent
      exit(0);
  }

  // read, write and excute
  // umask(0);
  pid_t sid = setsid();
  if (sid == -1) {
    LOG(ERROR) << "setsid failed: " << strerror(errno);
    return -1;
  }
  int rc = chdir("/");
  if (rc != 0) {
    LOG(ERROR) << "chdir failed: " << strerror(errno);
    return rc;
  }
  int fd = open("/dev/null", O_RDWR);
  if (fd == -1) {
    LOG(ERROR) << "open /dev/null failed";
    return -1;
  }
  // close 0, 1, 2
  for (int i = 0; i <= STDERR_FILENO; ++i) {
    if (dup2(i, fd) == -1) {
      LOG(ERROR) << "dup2 failed (oldfd, newfd): (" << i << ", " << fd << ")";
      return -1;
    }
  }
  return 0;
}

int fork_child(pid_t* child_id) {
  LOG(INFO) << "fork child";
  pid_t pid = fork();

  if (pid == -1) {
    LOG(FATAL) << "fork child failed: " << strerror(errno);
    exit(-1);
  }
  else if (pid == 0) {
    // child
    LOG(INFO) << "child proccess begin";
    return Main();
  } 
  else {
    LOG(INFO) << "fork child " << pid;
    struct timeval tv = { 0, 100 * 1000 };
    select(0, NULL, NULL, NULL, &tv);

    int status = 0;
    switch (waitpid(pid, &status, WNOHANG)) {
      case -1:
        LOG(ERROR) << "waidpid failed: " << strerror(errno);
        break;
      case 0:
        LOG(INFO) << "child spawned successfully, PID: " << pid;
        break;

      default:
        if (WIFEXITED(status)) {
          LOG(ERROR) << "child exited with: " << WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status)) {
          LOG(ERROR) << "child signaled: " << WTERMSIG(status);
        }
        else {
          LOG(ERROR) << "child died somehow: exit status = " << status;
        }
        return -1;
    }
    if (child_id) {
      *child_id = pid;
    }
    return 0;
  }
}

void signal_child(int sig) {
  LOG(INFO) << "signal child";
  int status = 0;

  pid_t pid = waitpid(-1, &status, WNOHANG);
  if (pid == 0) {
    return;
  }

  if (pid == -1) {
    LOG(ERROR) << "signal_child waitpid return -1: " << strerror(errno);
    return;
  }
  g_child_id = -1;
  if (WIFEXITED(status)) {
    LOG(INFO) << "master waitpid: " << WEXITSTATUS(status);
  }
  else if (WIFSIGNALED(status)) {
    LOG(INFO) << "master waitpid: " << WTERMSIG(status);
  }
  else {
    LOG(INFO) << "master waitpid: exit status = " << status;
  }
}
void signal_terminate(int sig) {
  g_quit = true;
  if (g_child_id != -1) {
    int ret = kill(g_child_id, sig);
    LOG(INFO) << "kill " << g_child_id << " " << sig << " ret " << ret;
  }
}

void install_signal(int sig, signal_handler handler) {
  struct sigaction new_action;
  new_action.sa_handler = handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(sig, &new_action, NULL);
}
int master_process_cycle() {
  LOG(INFO) << "enter master process cycle";
  install_signal(SIGCHLD, signal_child);
  install_signal(SIGINT, signal_terminate);
  install_signal(SIGTERM, signal_terminate);

  while (true) {
    int ret = pause();
    LOG(INFO) << "supspend once" << ret;
    if (g_quit) {
      LOG(INFO) << "normal quit";
      return 0;
    }
    if (g_child_id == -1) {
      g_child_id = -1;
      int ret = fork_child(&g_child_id);
      if (ret != 0) {
        LOG(ERROR) << "fork child failed";
      } else {
        LOG(INFO) << "fork child success";
      }
    }
  }

  return 0;
}
