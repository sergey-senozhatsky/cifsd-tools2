// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-cifsd-devel@lists.sourceforge.net
 */

#ifndef __MANAGEMENT_SHARE_H__
#define __MANAGEMENT_SHARE_H__

#include <glib.h>


enum share_users {
	/* Admin users */
	CIFSD_SHARE_ADMIN_USERS_MAP = 0,
	/* Valid users */
	CIFSD_SHARE_VALID_USERS_MAP,
	/* Invalid users */
	CIFSD_SHARE_INVALID_USERS_MAP,
	/* Read-only users */
	CIFSD_SHARE_READ_LIST_MAP,
	/* Read/Write access to a read-only share */
	CIFSD_SHARE_WRITE_LIST_MAP,
	CIFSD_SHARE_USERS_MAX,
};

enum share_hosts {
	CIFSD_SHARE_HOSTS_ALLOW_MAP = 0,
	CIFSD_SHARE_HOSTS_DENY_MAP,
	CIFSD_SHARE_HOSTS_MAX,
};

#define CIFSD_SHARE_DEFAULT_CREATE_MASK	0744
#define CIFSD_SHARE_DEFAULT_DIRECTORY_MASK	0755

struct cifsd_share {
	char		*name;
	char		*path;

	int		max_connections;
	int		num_connections;

	GRWLock		update_lock;
	int		ref_count;

	int		create_mask;
	int		directory_mask;
	int		flags;

	char		*veto_list;
	int		veto_list_sz;

	char		*guest_account;

	GHashTable	*maps[CIFSD_SHARE_USERS_MAX];
	/*
	 * FIXME
	 * We need to support IP ranges, netmasks, etc.
	 * This is just a silly hostname matching, hence
	 * these two are not in ->maps[].
	 */
	GHashTable	*hosts_allow_map;
	/* Deny access */
	GHashTable	*hosts_deny_map;

	/* One lock to rule them all [as of now] */
	GRWLock		maps_lock;

	char*		comment;
};

static void set_share_flag(struct cifsd_share *share, int flag)
{
	share->flags |= flag;
}

static void clear_share_flag(struct cifsd_share *share, int flag)
{
	share->flags &= ~flag;
}

static int test_share_flag(struct cifsd_share *share, int flag)
{
	return share->flags & flag;
}

struct cifsd_share *get_cifsd_share(struct cifsd_share *share);
void put_cifsd_share(struct cifsd_share *share);
struct cifsd_share *shm_lookup_share(char *name);

struct smbconf_group;
int shm_add_new_share(struct smbconf_group *group);

void shm_destroy(void);
int shm_init(void);

int shm_lookup_users_map(struct cifsd_share *share,
			  enum share_users map,
			  char *name);

int shm_lookup_hosts_map(struct cifsd_share *share,
			  enum share_hosts map,
			  char *host);

int shm_open_connection(struct cifsd_share *share);
int shm_close_connection(struct cifsd_share *share);

typedef void (*walk_shares)(gpointer key,
			    gpointer value,
			    gpointer user_data);
void for_each_cifsd_share(walk_shares cb, gpointer user_data);

struct cifsd_share_config_response;

int shm_share_config_payload_size(struct cifsd_share *share);
int shm_handle_share_config_request(struct cifsd_share *share,
				    struct cifsd_share_config_response *resp);

#endif /* __MANAGEMENT_SHARE_H__ */
