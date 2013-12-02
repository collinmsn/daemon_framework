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
static void install_signals();
static int retry_sleep_time();
extern int Main();

static pid_t g_child_id = -1;
static bool g_quit = false;

int main(int argc, char** argv) {
    google::InitGoogleLogging(argv[0]);
    google::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_daemon) {
        if (daemonize() != 0) {
            LOG(FATAL) << "daemonize failed";
            exit(-1);
        }
        LOG(INFO) << "daemonize success";
        
        install_signals();

        return master_process_cycle();
    }
    return Main();
}

int daemonize() {
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
    // do not change to '/'
    // int rc = chdir("/");
    // if (rc != 0) {
    //   LOG(ERROR) << "chdir failed: " << strerror(errno);
    //   return rc;
    // }
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
    g_child_id = -1;

    if (pid == 0) {
        LOG(WARNING) << "waitpid return 0";
        return;
    }

    if (pid == -1) {
        LOG(ERROR) << "signal_child waitpid return -1: " << strerror(errno);
        return;
    }

    if (WIFEXITED(status)) {
        LOG(INFO) << "master waitpid: child exited, status = " << WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
        LOG(INFO) << "master waitpid: child killed by signal " << WTERMSIG(status);
    }
    else if (WIFSTOPPED(status)) {
        LOG(INFO) << "master waitpid: child stopped by signal " << WSTOPSIG(status);
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

void install_signals() {
    LOG(INFO) << "install signals";
    install_signal(SIGCHLD, signal_child);
    install_signal(SIGINT, signal_terminate);
    install_signal(SIGTERM, signal_terminate);  
}
int master_process_cycle() {
    sigset_t set;
    sigemptyset(&set);

    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);

    // block signals, start child process, and then wait signals
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        LOG(FATAL) << "sigprocmask failed";
        exit(-1);
    }
    
    fork_child(&g_child_id);

    sigemptyset(&set);
    
    while (true) {
        sigsuspend(&set);
        LOG(INFO) << "supspend once";
        
        if (g_quit) {
            LOG(INFO) << "normal quit";
            return 0;
        }
        if (g_child_id == -1) {
            // child exit
            int sleep_time = retry_sleep_time();
            if (sleep_time != 0) {
                LOG(INFO) << "sleep " << sleep_time << " seconds before next retry";
                sleep(sleep_time);
            }
    
            fork_child(&g_child_id);
        }
    }

    return 0;
}

int retry_sleep_time() {
    const static int kMaxRetryInterval = 3 * 60;
    static int last_interval = 0;
    static int last_time = 0;
    int cur = time(NULL);
    if (cur - last_time > 2 * kMaxRetryInterval) {
        last_interval = 0;
    }
    else {
        last_interval = 2 * last_interval;
        if (last_interval == 0) {
            last_interval = 1;
        } else if (last_interval > kMaxRetryInterval) {
            last_interval = kMaxRetryInterval;
        }
    }
    last_time = cur;
    return last_interval;
}
