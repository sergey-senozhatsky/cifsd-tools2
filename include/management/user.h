// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-cifsd-devel@lists.sourceforge.net
 */

#ifndef __MANAGEMENT_USER_H__
#define __MANAGEMENT_USER_H__

#include <sys/types.h>
#include <pwd.h>
#include <glib.h>

struct cifsd_user {
	char		*name;
	char		*pass_b64;

	char		*pass;
	int		pass_sz;

	uid_t		uid;
	gid_t		gid;

	int		ref_count;
	int 		flags;
	GRWLock		update_lock;
};

static inline void set_user_flag(struct cifsd_user *user, int bit)
{
	user->flags |= bit;
}

static inline int test_user_flag(struct cifsd_user *user, int bit)
{
	return user->flags & bit;
}

struct cifsd_user *get_cifsd_user(struct cifsd_user *user);
void put_cifsd_user(struct cifsd_user *user);

struct cifsd_user *usm_lookup_user(char *name);

int usm_update_user_password(struct cifsd_user *user, char *pass);

int usm_add_new_user(char *name, char *pwd);
int usm_new_user_from_pwdentry(char *data);
int usm_update_user_from_pwdentry(char *data);

void usm_destroy(void);
int usm_init(void);

typedef void (*walk_users)(gpointer key,
			   gpointer value,
			   gpointer user_data);
void for_each_cifsd_user(walk_users cb, gpointer user_data);

struct cifsd_login_request;
struct cifsd_login_response;

int usm_handle_login_request(struct cifsd_login_request *req,
			     struct cifsd_login_response *resp);

#endif /* __MANAGEMENT_USER_H__ */
