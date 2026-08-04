#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "grokium_hub.h"
#include "grokium.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>
#include <time.h>

/* Default hub port — same as nanobot peer. */
#ifndef GKX_HUB_PORT
#define GKX_HUB_PORT 8787
#endif

static void hub_dir(char *out, size_t n) {
  snprintf(out, n, "%s/data/hub", root[0] ? root : ".");
}

static void pid_path(char *out, size_t n) {
  char d[PATH_MAX];
  hub_dir(d, sizeof d);
  snprintf(out, n, "%s/nanobot.pid", d);
}

static void lock_path_default(char *out, size_t n) {
  const char *rt = getenv("XDG_RUNTIME_DIR");
  if (rt && rt[0])
    snprintf(out, n, "%s/nanobot-llm.lock", rt);
  else
    snprintf(out, n, "/tmp/nanobot-llm-%d.lock", (int)getuid());
}

void gkx_hub_apply_sched_env(const gkx_config *cfg) {
  char lock[PATH_MAX];
  lock_path_default(lock, sizeof lock);
  setenv("NANOBOT_LLM_LOCK", lock, 0); /* don't override user */
  setenv("NANOBOT_LLM_SERIAL", "1", 0);
  /* Match llama parallel if set; default 1 (safe). Allow 2-3 for :1212 multi-slot. */
  const char *slots = getenv("NANOBOT_LLM_SLOTS");
  if (!slots || !slots[0]) {
    const char *want = "1";
    if (cfg && cfg->context_window > 0) {
      /* keep default 1 unless user opts in */
      (void)want;
    }
    setenv("NANOBOT_LLM_SLOTS", "1", 0);
  }
  /* Shared home for hub process */
  char home[PATH_MAX];
  hub_dir(home, sizeof home);
  mkdir(home, 0700);
  {
    char nh[PATH_MAX];
    snprintf(nh, sizeof nh, "%s/nanobot_home", home);
    mkdir(nh, 0700);
    setenv("NANOBOT_HOME", nh, 0);
  }
}

static int read_pid(void) {
  char p[PATH_MAX], line[64];
  pid_path(p, sizeof p);
  FILE *f = fopen(p, "r");
  if (!f) return -1;
  if (!fgets(line, sizeof line, f)) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return atoi(line);
}

static int pid_alive(int pid) {
  if (pid <= 0) return 0;
  return kill(pid, 0) == 0 || errno == EPERM;
}

static int hub_http_ok(void) {
  char cmd[256];
  snprintf(cmd, sizeof cmd,
           "curl -sS -m 1 -o /dev/null -w '%%{http_code}' "
           "http://127.0.0.1:%d/peer/v1/health 2>/dev/null",
           GKX_HUB_PORT);
  FILE *p = popen(cmd, "r");
  if (!p) return 0;
  char code[16] = "";
  if (fgets(code, sizeof code, p)) { /* ok */ }
  pclose(p);
  return atoi(code) == 200;
}

static const char *find_nanobot_bin(void) {
  static char path[PATH_MAX];
  const char *e = getenv("NANOBOT_BIN");
  if (e && e[0] && access(e, X_OK) == 0) return e;
  {
    char try[PATH_MAX];
    snprintf(try, sizeof try, "%s/deps/nanobot/build/host/nanobot", root);
    if (access(try, X_OK) == 0) {
      snprintf(path, sizeof path, "%s", try);
      return path;
    }
    const char *home = getenv("HOME");
    if (home) {
      snprintf(try, sizeof try, "%s/Dev/AI/nanobot/build/host/nanobot", home);
      if (access(try, X_OK) == 0) {
        snprintf(path, sizeof path, "%s", try);
        return path;
      }
    }
  }
  if (access("/usr/local/bin/nanobot", X_OK) == 0) return "/usr/local/bin/nanobot";
  if (access("/usr/bin/nanobot", X_OK) == 0) return "/usr/bin/nanobot";
  e = getenv("PATH");
  (void)e;
  return "nanobot";
}

int gkx_hub_status(char *buf, size_t n) {
  int pid = read_pid();
  int alive = pid_alive(pid);
  int http = hub_http_ok();
  snprintf(buf, n, "hub port=%d pid=%d alive=%d http=%d lock=%s slots=%s",
           GKX_HUB_PORT, pid, alive, http,
           getenv("NANOBOT_LLM_LOCK") ? getenv("NANOBOT_LLM_LOCK") : "(default)",
           getenv("NANOBOT_LLM_SLOTS") ? getenv("NANOBOT_LLM_SLOTS") : "1");
  return (alive && http) ? 0 : 1;
}

int gkx_hub_stop(void) {
  int pid = read_pid();
  if (pid > 0 && pid_alive(pid)) {
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
      if (!pid_alive(pid)) break;
      usleep(100000);
    }
    if (pid_alive(pid)) kill(pid, SIGKILL);
  }
  char p[PATH_MAX];
  pid_path(p, sizeof p);
  unlink(p);
  return 0;
}

int gkx_hub_ensure(const gkx_config *cfg) {
  gkx_hub_apply_sched_env(cfg);
  if (hub_http_ok()) return 0;
  int pid = read_pid();
  if (pid_alive(pid) && hub_http_ok()) return 0;
  if (pid_alive(pid) && !hub_http_ok())
    gkx_hub_stop();

  const char *bin = find_nanobot_bin();
  char home[PATH_MAX], logpath[PATH_MAX], d[PATH_MAX];
  hub_dir(d, sizeof d);
  mkdir(d, 0700);
  snprintf(home, sizeof home, "%s/nanobot_home", d);
  mkdir(home, 0700);
  snprintf(logpath, sizeof logpath, "%s/hub.log", d);

  const char *base = (cfg && cfg->local_base_url[0])
                         ? cfg->local_base_url
                         : "http://127.0.0.1:1212/v1";

  pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    int logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd >= 0) {
      dup2(logfd, 1);
      dup2(logfd, 2);
      close(logfd);
    }
    setsid();
    setenv("NANOBOT_HOME", home, 1);
    gkx_hub_apply_sched_env(cfg);
    char port[16];
    snprintf(port, sizeof port, "%d", GKX_HUB_PORT);
    /* Hub: offline local, peer IN, optional OUT at +1 */
    execlp(bin, "nanobot",
           "--offline",
           "--base-url", base,
           "--model", "local",
           "--port", port,
           "--hub",
           (char *)NULL);
    _exit(127);
  }

  /* parent: wait for health */
  char pp[PATH_MAX];
  pid_path(pp, sizeof pp);
  FILE *f = fopen(pp, "w");
  if (f) {
    fprintf(f, "%d\n", (int)child);
    fclose(f);
  }
  for (int i = 0; i < 50; i++) {
    if (hub_http_ok()) return 0;
    usleep(100000);
  }
  return hub_http_ok() ? 0 : -1;
}
