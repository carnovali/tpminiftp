// signal.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>   // for pid_t
#include <syslog.h>

#include "signals.h"
#include "session.h"    // for current_sess
#include "utils.h"      // for close_fd()

int server_socket = -1;

static void handle_sigint(int sig) {
  (void)sig;
  static volatile sig_atomic_t in_handler = 0;


  if (in_handler) {
    syslog(LOG_WARNING, "SIGINT handler reentered!");
    return; // Avoid running handler twice concurrently
  }
  in_handler = 1;

  static int sigint_count = 0;
  syslog(LOG_INFO, "SIGINT handler called (count = %d) in PID %d", ++sigint_count, getpid());

  syslog(LOG_INFO, "[+] SIGINT received. Shutting down...");

  // Close listening socket
  if (server_socket >= 0) {
    close_fd(server_socket,"listen socket");
    server_socket = -1;
  }

  // Block SIGINT while performing shutdown
  // avoid problems when multiple signals arrive
  sigset_t blockset, oldset;
  sigemptyset(&blockset);
  sigaddset(&blockset, SIGINT);
  if (sigprocmask(SIG_BLOCK, &blockset, &oldset) < 0) {
    syslog(LOG_ERR, "sigprocmask: %m");
  }

  // Restore previous signal mask (optional here since we're exiting)
  sigprocmask(SIG_SETMASK, &oldset, NULL);


  exit(EXIT_SUCCESS);
}

// --- SIGTERM handler for parent ---
static void handle_sigterm(int sig) {
  (void)sig;

  static volatile sig_atomic_t in_handler = 0;
  if (in_handler) {
    syslog(LOG_WARNING, "SIGTERM handler reentered!");
    return;
  }
  in_handler = 1;

  syslog(LOG_INFO, "[+] SIGTERM received. Shutting down (PID %d)...", getpid());

  // Close listening socket if open
  if (server_socket >= 0) {
    close_fd(server_socket, "listen socket");
    server_socket = -1;
  }

  exit(EXIT_SUCCESS);
}

void setup_signals(void) {
  struct sigaction sa;

  syslog(LOG_DEBUG, "Setting up signal handlers in PID %d", getpid());

  // Setup SIGINT and SIGTERM for parent

  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGINT);  // Block SIGINT while handler runs

  sa.sa_flags = SA_RESTART;        // Restart interrupted syscalls

  sa.sa_handler = handle_sigint;

  // Handle SIGINT
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigaction SIGINT: %m");
    exit(EXIT_FAILURE);
  }
  syslog(LOG_DEBUG, "SIGINT handler installed in PID %d", getpid());

  // Handle SIGTERM, same mask and flags, but different handler
  sa.sa_handler = handle_sigterm;

  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigaction SIGTERM: %m");
    exit(EXIT_FAILURE);
  }
}

// --- Restore all defaults (used by children before exec or clean exit)
void reset_signals(void) {
  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = SIG_DFL;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}
