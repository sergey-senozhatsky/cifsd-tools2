/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 *
 *   linux-cifsd-devel@lists.sourceforge.net
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <memory.h>
#include <endian.h>
#include <glib.h>
#include <errno.h>
#include <linux/cifsd_server.h>

#include <management/share.h>

#include <rpc.h>
#include <rpc_srvsvc.h>
#include <cifsdtools.h>

#define SHARE_TYPE_TEMP			0x40000000
#define SHARE_TYPE_HIDDEN		0x80000000

#define SHARE_TYPE_DISKTREE		0
#define SHARE_TYPE_DISKTREE_TEMP	(SHARE_TYPE_DISKTREE|SHARE_TYPE_TEMP)
#define SHARE_TYPE_DISKTREE_HIDDEN	(SHARE_TYPE_DISKTREE|SHARE_TYPE_HIDDEN)
#define SHARE_TYPE_PRINTQ		1
#define SHARE_TYPE_PRINTQ_TEMP		(SHARE_TYPE_PRINTQ|SHARE_TYPE_TEMP)
#define SHARE_TYPE_PRINTQ_HIDDEN	(SHARE_TYPE_PRINTQ|SHARE_TYPE_HIDDEN)
#define SHARE_TYPE_DEVICE		2
#define SHARE_TYPE_DEVICE_TEMP		(SHARE_TYPE_DEVICE|SHARE_TYPE_TEMP)
#define SHARE_TYPE_DEVICE_HIDDEN	(SHARE_TYPE_DEVICE|SHARE_TYPE_HIDDEN)
#define SHARE_TYPE_IPC			3
#define SHARE_TYPE_IPC_TEMP		(SHARE_TYPE_IPC|SHARE_TYPE_TEMP)
#define SHARE_TYPE_IPC_HIDDEN		(SHARE_TYPE_IPC|SHARE_TYPE_HIDDEN)

#define SRVSVC_OPNUM_SHARE_ENUM_ALL	15
#define SRVSVC_OPNUM_GET_SHARE_INFO 	16

static int __share_type(struct cifsd_share *share)
{
	if (test_share_flag(share, CIFSD_SHARE_FLAG_PIPE))
		return SHARE_TYPE_IPC;
	if (!g_ascii_strncasecmp(share->name, "IPC", strlen("IPC")))
		return SHARE_TYPE_IPC;
	return SHARE_TYPE_DISKTREE;
}

static int __share_entry_size_ctr0(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;

	return strlen(share->name) * 2 + 4 * sizeof(__u32);
}

static int __share_entry_size_ctr1(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;
	int sz;

	sz = strlen(share->name) * 2 + strlen(share->comment) * 2;
	sz += 9 * sizeof(__u32);
	return sz;
}

/*
 * Embedded Reference Pointers
 *
 * An embedded reference pointer is represented in two parts, a 4 octet
 * value in place and a possibly deferred representation of the referent.
 */
static int __share_entry_rep_ctr0(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;

	dce->num_pointers++;
	return ndr_write_int32(dce, dce->num_pointers); /* ref pointer */
}

static int __share_entry_rep_ctr1(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;
	int ret;

	dce->num_pointers++;
	ret = ndr_write_int32(dce, dce->num_pointers); /* ref pointer */
	ret |= ndr_write_int32(dce, __share_type(share));
	dce->num_pointers++;
	ret |= ndr_write_int32(dce, dce->num_pointers); /* ref pointer */
	return ret;
}

static int __share_entry_data_ctr0(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;

	return ndr_write_vstring(dce, share->name);
}

static int __share_entry_data_ctr1(struct cifsd_dcerpc *dce, gpointer entry)
{
	struct cifsd_share *share = entry;
	int ret;

	ret = ndr_write_vstring(dce, share->name);
	ret |= ndr_write_vstring(dce, share->comment);
	return ret;
}

static int __share_entry_processed(struct cifsd_rpc_pipe *pipe, int i)
{
	struct cifsd_share *share;

	share = g_array_index(pipe->entries,  gpointer, i);
	pipe->entries = g_array_remove_index(pipe->entries, i);
	pipe->num_entries--;
	put_cifsd_share(share);
}

static void __enum_all_shares(gpointer key, gpointer value, gpointer user_data)
{
	struct cifsd_rpc_pipe *pipe = (struct cifsd_rpc_pipe *)user_data;
	struct cifsd_share *share = (struct cifsd_share *)value;

	if (!get_cifsd_share(share))
		return;

	if (!test_share_flag(share, CIFSD_SHARE_FLAG_BROWSEABLE)) {
		put_cifsd_share(share);
		return;
	}

	if (!test_share_flag(share, CIFSD_SHARE_FLAG_AVAILABLE)) {
		put_cifsd_share(share);
		return;
	}

	pipe->entries = g_array_append_val(pipe->entries, share);
	pipe->num_entries++;
}

static int srvsvc_share_enum_all_invoke(struct cifsd_rpc_pipe *pipe)
{
	for_each_cifsd_share(__enum_all_shares, pipe);
	pipe->entry_processed = __share_entry_processed;
	return 0;
}

static int srvsvc_share_get_info_invoke(struct cifsd_rpc_pipe *pipe,
					struct srvsvc_share_info_request *hdr)
{
	struct cifsd_share *share;

	share = shm_lookup_share(hdr->share_name.ptr);
	if (!share)
		return -EINVAL;

	pipe->entries = g_array_append_val(pipe->entries, share);
	pipe->num_entries++;
	pipe->entry_processed = __share_entry_processed;
	return 0;
}

static int srvsvc_share_enum_all_return(struct cifsd_rpc_pipe *pipe)
{
	struct cifsd_dcerpc *dce = pipe->dce;
	int status;

	ndr_write_union_int32(dce, dce->si_req.level);
	ndr_write_int32(dce, pipe->num_entries);

	status = ndr_write_array_of_structs(pipe);
	/*
	 * [out] DWORD* TotalEntries
	 * [out, unique] DWORD* ResumeHandle
	 */
	ndr_write_int32(dce, pipe->num_entries);
	if (status == CIFSD_RPC_EMORE_DATA) {
		ndr_write_int32(dce, 0x01);
		/* Have pending data, set RETURN_READY again */
		dce->flags |= CIFSD_DCERPC_RETURN_READY;
	} else {
		ndr_write_int32(dce, 0x00);
	}
	return status;
}

static int srvsvc_share_get_info_return(struct cifsd_rpc_pipe *pipe)
{
	struct cifsd_dcerpc *dce = pipe->dce;

	ndr_write_union_int32(dce, dce->si_req.level);
	return __ndr_write_array_of_structs(pipe, 1);
}

static int srvsvc_parse_share_info_req(struct cifsd_dcerpc *dce,
				       struct srvsvc_share_info_request *hdr)
{
	ndr_read_uniq_vsting_ptr(dce, &hdr->server_name);

	if (dce->req_hdr.opnum == SRVSVC_OPNUM_SHARE_ENUM_ALL) {
		int ptr;

		hdr->level = ndr_read_int32(dce);
		ndr_read_int32(dce); // read switch selector
		ndr_read_int32(dce); // read container pointer ref id
		ndr_read_int32(dce); // read container array size
		ptr = ndr_read_int32(dce); // read container array pointer
					   // it should be null
		if (ptr != 0x00) {
			pr_err("SRVSVC: container array pointer is %p\n",
				ptr);
			return -EINVAL;
		}
		hdr->max_size = ndr_read_int32(dce);
		ndr_read_uniq_ptr(dce, &hdr->payload_handle);
		return 0;
	}

	if (dce->req_hdr.opnum == SRVSVC_OPNUM_GET_SHARE_INFO) {
		ndr_read_vstring_ptr(dce, &hdr->share_name);
		hdr->level = ndr_read_int32(dce);
		return 0;
	}

	return -ENOTSUP;
}

static int srvsvc_share_info_invoke(struct cifsd_rpc_pipe *pipe)
{
	struct cifsd_dcerpc *dce;
	int ret;

	dce = pipe->dce;
	if (srvsvc_parse_share_info_req(dce, &dce->si_req))
		return CIFSD_RPC_EBAD_DATA;

	pipe->entry_processed = __share_entry_processed;

	if (dce->req_hdr.opnum == SRVSVC_OPNUM_GET_SHARE_INFO)
		ret = srvsvc_share_get_info_invoke(pipe, &dce->si_req);
	if (dce->req_hdr.opnum == SRVSVC_OPNUM_SHARE_ENUM_ALL)
		ret = srvsvc_share_enum_all_invoke(pipe);
	return ret;
}

static int srvsvc_share_info_return(struct cifsd_rpc_pipe *pipe)
{
	struct cifsd_dcerpc *dce = pipe->dce;
	int ret = CIFSD_RPC_OK, status;

	/*
	 * Reserve space for response NDR header. We don't know yet if
	 * the payload buffer is big enough. This will determine if we
	 * can set DCERPC_PFC_FIRST_FRAG|DCERPC_PFC_LAST_FRAG or if we
	 * will have a multi-part response.
	 */
	dce->offset = sizeof(struct dcerpc_header);
	dce->offset += sizeof(struct dcerpc_response_header);

	if (dce->si_req.level == 0) {
		dce->entry_size = __share_entry_size_ctr0;
		dce->entry_rep = __share_entry_rep_ctr0;
		dce->entry_data = __share_entry_data_ctr0;
	} else if (dce->si_req.level == 1) {
		dce->entry_size = __share_entry_size_ctr1;
		dce->entry_rep = __share_entry_rep_ctr1;
		dce->entry_data = __share_entry_data_ctr1;
	} else {
		status = CIFSD_RPC_EINVALID_LEVEL;
		rpc_pipe_reset(pipe);
	}

	if (dce->req_hdr.opnum == SRVSVC_OPNUM_GET_SHARE_INFO)
		status = srvsvc_share_get_info_return(pipe);
	if (dce->req_hdr.opnum == SRVSVC_OPNUM_SHARE_ENUM_ALL)
		status = srvsvc_share_enum_all_return(pipe);

	/*
	 * [out] DWORD Return value/code
	 */
	if (ret != CIFSD_RPC_OK)
		status = ret;

	ndr_write_int32(dce, status);
	dcerpc_write_headers(dce, status);

	dce->rpc_resp->payload_sz = dce->offset;
	return ret;
}

static int srvsvc_invoke(struct cifsd_rpc_pipe *pipe)
{
	int ret = CIFSD_RPC_ENOTIMPLEMENTED;

	switch (pipe->dce->req_hdr.opnum) {
	case SRVSVC_OPNUM_SHARE_ENUM_ALL:
	case SRVSVC_OPNUM_GET_SHARE_INFO:
		ret = srvsvc_share_info_invoke(pipe);
		break;
	default:
		pr_err("SRVSVC: unsupported INVOKE method %d\n",
		       pipe->dce->req_hdr.opnum);
		break;
	}

	return ret;
}

static int srvsvc_return(struct cifsd_rpc_pipe *pipe,
			 struct cifsd_rpc_command *resp,
			 int max_resp_sz)
{
	struct cifsd_dcerpc *dce = pipe->dce;
	int ret;

	switch (dce->req_hdr.opnum) {
	case SRVSVC_OPNUM_SHARE_ENUM_ALL:
		if (dce->si_req.max_size < (unsigned int)max_resp_sz)
			max_resp_sz = dce->si_req.max_size;
	case SRVSVC_OPNUM_GET_SHARE_INFO:
		dcerpc_set_ext_payload(dce, resp->payload, max_resp_sz);

		ret = srvsvc_share_info_return(pipe);
		break;
	default:
		pr_err("SRVSVC: unsupported RETURN method %d\n",
			dce->req_hdr.opnum);
		ret = CIFSD_RPC_EBAD_FUNC;
		break;
	}
	return ret;
}

int rpc_srvsvc_read_request(struct cifsd_rpc_pipe *pipe,
			    struct cifsd_rpc_command *resp,
			    int max_resp_sz)
{
	return srvsvc_return(pipe, resp, max_resp_sz);
}

int rpc_srvsvc_write_request(struct cifsd_rpc_pipe *pipe)
{
	return srvsvc_invoke(pipe);
}
