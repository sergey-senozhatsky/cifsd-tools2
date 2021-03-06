// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-cifsd-devel@lists.sourceforge.net
 */

#ifndef __CIFSDTOOLS_H__
#define __CIFSDTOOLS_H__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <getopt.h>
#include <glib.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

struct smbconf_global {
	int			flags;
	int			map_to_guest;
	char			*guest_account;

	char			*server_string;
	char			*work_group;
	char			*netbios_name;
	char			*server_min_protocol;
	char			*server_max_protocol;
	char			*root_dir;
	int			server_signing;
	int			sessions_cap;
	int			restrict_anon;
	unsigned short		tcp_port;
	unsigned short		ipc_timeout;
	unsigned int		deadtime;
	int			bind_interfaces_only;
	char			**interfaces;
	unsigned long		file_max;
	unsigned int		smb2_max_read;
	unsigned int		smb2_max_write;
	unsigned int		smb2_max_trans;
};

#define CIFSD_LOCK_FILE		"/tmp/cifsd.lock"

#define CIFSD_RESTRICT_ANON_TYPE_1	1
#define CIFSD_RESTRICT_ANON_TYPE_2	2

extern struct smbconf_global global_conf;

#define CIFSD_CONF_MAP_TO_GUEST_NEVER		(0)
#define CIFSD_CONF_MAP_TO_GUEST_BAD_USER	(1 << 0)
#define CIFSD_CONF_MAP_TO_GUEST_BAD_PASSWORD	(1 << 1)
#define CIFSD_CONF_MAP_TO_GUEST_BAD_UID		(1 << 2)

#define CIFSD_CONF_DEFAULT_NETBIOS_NAME	"CIFSD SERVER"
#define CIFSD_CONF_DEFAULT_SERVER_STRING	"CIFSD"
#define CIFSD_CONF_DEFAULT_WORK_GROUP		"WORKGROUP"

#define CIFSD_CONF_DEFAULT_GUEST_ACCOUNT	"nobody"
#define CIFSD_CONF_FALLBACK_GUEST_ACCOUNT	"ftp"

#define CIFSD_CONF_DEFAULT_SESS_CAP	1024
#define CIFSD_CONF_DEFAULT_TPC_PORT	445

#define CIFSD_CONF_FILE_MAX		10000

#define PATH_PWDDB	"/etc/cifs/cifsdpwd.db"
#define PATH_SMBCONF	"/etc/cifs/smb.conf"

#define CIFSD_HEALTH_START		(0)
#define CIFSD_HEALTH_RUNNING		(1 << 0)
#define CIFSD_SHOULD_RELOAD_CONFIG	(1 << 1)

extern int cifsd_health_status;

#define TRACING_DUMP_NL_MSG	0

#define ARRAY_SIZE(X) (sizeof(X) / sizeof((X)[0]))

//---------------------------------------------------------------//
#define LOGAPP		"[%s/%d]:"
#define PRERR		LOGAPP" ERROR: "
#define PRINF		LOGAPP" INFO: "
#define PRDEBUG		LOGAPP" DEBUG: "

#define PR_ERROR	0
#define PR_INFO		1
#define PR_DEBUG	2

static int log_level = PR_INFO;

#define PR_LOGGER_STDIO         0
#define PR_LOGGER_SYSLOG        1

/*
 * A thread-safe strerror() wrapper, uses static TLS buffer.
 * NOTE: a signal handler can concurrent modify the buffer,
 * but the buffer should always be nul-terminated.
 */
char *strerr(int err);

__attribute__ ((format (printf, 2, 3)))
extern void __pr_log(int level, const char *fmt,...);
extern void set_logger_app_name(const char *an);
extern const char *get_logger_app_name(void);
extern void pr_logger_init(int flags);

#define pr_log(l, f, ...)						\
	do {								\
		if ((l) <= log_level)					\
			__pr_log((l), (f), get_logger_app_name(),	\
					getpid(),			\
					##__VA_ARGS__);			\
	} while (0)

#define pr_debug(f,...)	\
	pr_log(PR_DEBUG, PRDEBUG f, ##__VA_ARGS__)
#define pr_info(f,...)	\
	pr_log(PR_INFO, PRINF f, ##__VA_ARGS__)
#define pr_err(f,...)	\
	pr_log(PR_ERROR, PRERR f, ##__VA_ARGS__)

//---------------------------------------------------------------//

void pr_hex_dump(const void *mem, size_t sz);

char *base64_encode(unsigned char *src, size_t srclen);
unsigned char *base64_decode(char const *src, size_t *dstlen);

gchar *cifsd_gconvert(const gchar *str,
		      gssize       str_len,
		      int          to_codeset,
		      int          from_codeset,
		      gsize       *bytes_read,
		      gsize       *bytes_written);

enum charset_idx {
	CIFSD_CHARSET_UTF8		= 0,
	CIFSD_CHARSET_UTF16LE,
	CIFSD_CHARSET_UCS2LE,
	CIFSD_CHARSET_UTF16BE,
	CIFSD_CHARSET_UCS2BE,
	CIFSD_CHARSET_MAX		= 5,
};

#define CIFSD_CHARSET_DEFAULT		CIFSD_CHARSET_UTF8

extern char *cifsd_conv_charsets[CIFSD_CHARSET_MAX + 1];

void notify_cifsd_daemon(void);
int test_file_access(char *conf);

#endif /* __CIFSDTOOLS_H__ */
