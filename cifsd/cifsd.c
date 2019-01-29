// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-cifsd-devel@lists.sourceforge.net
 */

#include <cifsdtools.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <ipc.h>
#include <rpc.h>
#include <worker.h>
#include <config_parser.h>
#include <management/user.h>
#include <management/share.h>
#include <management/session.h>
#include <management/tree_conn.h>

static int no_detach = 0;
int cifsd_health_status;
static pid_t worker_pid;
static int lock_fd = -1;
static char *pwddb = PATH_PWDDB;
static char *smbconf = PATH_SMBCONF;

typedef int (*worker_fn)(void);

static void usage(void)
{
	fprintf(stderr, "cifsd-tools version : %s\n", CIFSD_TOOLS_VERSION);
	fprintf(stderr, "Usage: cifsd\n");
	fprintf(stderr, "\t-p tcp port NUM | --port=NUM\n");
	fprintf(stderr, "\t-c smb.conf | --config=smb.conf\n");
	fprintf(stderr, "\t-i cifspwd.db | --import-users=cifspwd.db\n");
	fprintf(stderr, "\t-n | --nodetach\n");
	fprintf(stderr, "\t-s systemd service mode | --systemd\n");

	exit(EXIT_FAILURE);
}

static int create_lock_file()
{
	char manager_pid[10];
	size_t sz;

	lock_fd = open(CIFSD_LOCK_FILE, O_CREAT | O_EXCL | O_WRONLY,
			S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
	if (lock_fd < 0)
		return -EINVAL;

	if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
		return -EINVAL;

	sz = snprintf(manager_pid, sizeof(manager_pid), "%d", getpid());
	if (write(lock_fd, manager_pid, sz) == -1)
		pr_err("Unable to record main PID: %s\n", strerror(errno));
	return 0;
}

static void delete_lock_file()
{
	if (lock_fd == -1)
		return;

	flock(lock_fd, LOCK_UN);
	close(lock_fd);
	lock_fd = -1;
	remove(CIFSD_LOCK_FILE);
}

static int wait_group_kill(int signo)
{
	pid_t pid;
	int status;

	if (kill(worker_pid, signo) != 0)
		pr_err("can't execute kill %d: %s\n",
			worker_pid,
			strerror(errno));

	while (1) {
		pid = waitpid(-1, &status, 0);
		if (pid != 0) {
			pr_debug("detected pid %d termination\n", pid);
			break;
		}
		sleep(1);
	}
	return 0;
}

static int setup_signal_handler(int signo, sighandler_t handler)
{
	int status;
	sigset_t full_set;
	struct sigaction act;

	sigfillset(&full_set);
	memset(&act, 0, sizeof(act));

	act.sa_handler = handler;
	act.sa_mask = full_set;

	status = sigaction(signo, &act, NULL);
	if (status != 0)
		pr_err("Unable to register %s signal handler: %s",
				strsignal(signo), strerror(errno));
	return status;
}

static int setup_signals(sighandler_t handler)
{
	if (setup_signal_handler(SIGINT, handler) != 0)
		return -EINVAL;

	if (setup_signal_handler(SIGTERM, handler) != 0)
		return -EINVAL;

	if (setup_signal_handler(SIGABRT, handler) != 0)
		return -EINVAL;

	if (setup_signal_handler(SIGQUIT, handler) != 0)
		return -EINVAL;

	if (setup_signal_handler(SIGHUP, handler) != 0)
		return -EINVAL;

	if (setup_signal_handler(SIGSEGV, handler) != 0)
		return -EINVAL;

	return 0;
}

static int parse_configs(char *pwddb, char *smbconf)
{
	int ret;

	ret = cp_parse_pwddb(pwddb);
	if (ret) {
		pr_err("Unable to parse user database\n");
		return ret;
	}

	ret = cp_parse_smbconf(smbconf);
	if (ret) {
		pr_err("Unable to parse smb configuration file\n");
		return ret;
	}
	return 0;
}

static void worker_process_free(void)
{
	/*
	 * NOTE, this is the final release, we don't look at ref_count
	 * values. User management should be destroyed last.
	 */
	ipc_destroy();
	rpc_destroy();
	wp_destroy();
	sm_destroy();
	shm_destroy();
	usm_destroy();
}

static void child_sig_handler(int signo)
{
	if (signo == SIGHUP) {
		/*
		 * This is a signal handler, we can't take any locks, set
		 * a flag and wait for normal execution context to re-read
		 * the configs.
		 */
		cifsd_health_status |= CIFSD_SHOULD_RELOAD_CONFIG;
		pr_debug("Scheduled a config reload action.\n");
		return;
	}

	pr_err("Child received signal: %d (%s)\n",
		signo, strsignal(signo));

	worker_process_free();
	exit(EXIT_SUCCESS);
}

static void manager_sig_handler(int signo)
{
	/*
	 * Pass SIGHUP to worker, so it will reload configs
	 */
	if (signo == SIGHUP) {
		if (!worker_pid)
			return;

		cifsd_health_status |= CIFSD_SHOULD_RELOAD_CONFIG;
		if (kill(worker_pid, signo))
			pr_err("Unable to send SIGHUP to %d: %s\n",
				worker_pid, strerror(errno));
		return;
	}

	setup_signals(SIG_DFL);
	wait_group_kill(signo);
	pr_info("Exiting. Bye!\n");
	delete_lock_file();
	kill(0, SIGINT);
}

static int parse_reload_configs(const char *pwddb, const char *smbconf)
{
	int ret;

	ret = cp_parse_pwddb(pwddb);
	if (ret) {
		pr_err("Unable to parse-reload user database\n");
		return ret;
	}
	return 0;
}

static int worker_process_init(void)
{
	int ret;

	setup_signals(child_sig_handler);
	set_logger_app_name("cifsd-worker");

	ret = usm_init();
	if (ret) {
		pr_err("Failed to init user management\n");
		goto out;
	}

	ret = shm_init();
	if (ret) {
		pr_err("Failed to init net share management\n");
		goto out;
	}

	ret = parse_configs(pwddb, smbconf);
	if (ret) {
		pr_err("Failed to parse configuration files\n");
		goto out;
	}

	ret = sm_init();
	if (ret) {
		pr_err("Failed to init user session management\n");
		goto out;
	}

	ret = wp_init();
	if (ret) {
		pr_err("Failed to init worker threads pool\n");
		goto out;
	}

	ret = rpc_init();
	if (ret) {
		pr_err("Failed to init RPC subsystem\n");
		goto out;
	}

	ret = ipc_init();
	if (ret) {
		pr_err("Failed to init IPC subsystem\n");
		goto out;
	}

	while (cifsd_health_status & CIFSD_HEALTH_RUNNING) {
		if (cifsd_health_status & CIFSD_SHOULD_RELOAD_CONFIG) {
			ret = parse_reload_configs(pwddb, smbconf);
			if (ret)
				pr_err("Failed to reload configs. "
					"Continue with the old one.\n");

			ret = 0;
			cifsd_health_status &= ~CIFSD_SHOULD_RELOAD_CONFIG;
		}

		ret = ipc_process_event();
		if (ret)
			break;
	}
out:
	worker_process_free();
	return ret;
}

static pid_t start_worker_process(worker_fn fn)
{
	int status = 0;
	pid_t __pid;

	__pid = fork();
	if (__pid < 0) {
		pr_err("Can't fork child process: `%s'\n", strerror(errno));
		return -EINVAL;
	}
	if (__pid == 0) {
		status = fn();
		exit(status);
	}
	return __pid;
}

static int manager_process_init(void)
{
	int ret;

	setup_signals(manager_sig_handler);
	if (no_detach == 0) {
		pr_logger_init(PR_LOGGER_SYSLOG);
		if (daemon(0, 0) != 0) {
			pr_err("Daemonization failed\n");
			goto out;
		}
	}

	if (create_lock_file()) {
		pr_err("Failed to create lock file: %s\n", strerror(errno));
		goto out;
	}

	worker_pid = start_worker_process(worker_process_init);
	if (worker_pid < 0)
		goto out;

	while (1) {
		int status;
		pid_t child;

		child = waitpid(-1, &status, 0);
		if (cifsd_health_status & CIFSD_SHOULD_RELOAD_CONFIG &&
				errno == EINTR) {
			cifsd_health_status &= ~CIFSD_SHOULD_RELOAD_CONFIG;
			continue;
		}

		pr_err("WARNING: child process exited abnormally: %d\n",
				child);
		if (child == -1) {
			pr_err("waitpid() returned error code: %s\n",
				strerror(errno));
			goto out;
		}

		/* Ratelimit automatic restarts */
		sleep(1);
		worker_pid = start_worker_process(worker_process_init);
		if (worker_pid < 0)
			goto out;
	}
out:
	delete_lock_file();
	kill(0, SIGTERM);
	return ret;
}

static int manager_systemd_service(void)
{
	pid_t __pid;

	__pid = start_worker_process(manager_process_init);
	if (__pid < 0)
		return -EINVAL;

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = EXIT_FAILURE;
	int systemd_service = 0;
	int c;

	set_logger_app_name("cifsd-manager");
	memset(&global_conf, 0x00, sizeof(struct smbconf_global));
	pr_logger_init(PR_LOGGER_STDIO);

	opterr = 0;
	while ((c = getopt(argc, argv, "p:c:i:snh")) != EOF)
		switch (c) {
		case 'p':
			global_conf.tcp_port = cp_get_group_kv_long(optarg);
			pr_debug("TCP port option override\n");
			break;
		case 'c':
			smbconf = strdup(optarg);
			break;
		case 'i':
			pwddb = strdup(optarg);
			break;
		case 'n':
			no_detach = 1;
			break;
		case 's':
			systemd_service = 1;
			break;
		case '?':
		case 'h':
		default:
			usage();
	}

	if (!smbconf || !pwddb) {
		pr_err("Out of memory\n");
		exit(EXIT_FAILURE);
	}

	setup_signals(manager_sig_handler);
	if (!systemd_service)
		return manager_process_init();
	return manager_systemd_service();
}
