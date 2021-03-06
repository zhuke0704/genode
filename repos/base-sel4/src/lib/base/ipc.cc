/*
 * \brief  seL4 implementation of the IPC API
 * \author Norman Feske
 * \date   2014-10-14
 */

/*
 * Copyright (C) 2014 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* Genode includes */
#include <base/ipc.h>
#include <base/printf.h>
#include <base/blocking.h>
#include <base/thread.h>
#include <util/misc_math.h>

/* base-internal includes */
#include <base/internal/capability_space_sel4.h>
#include <base/internal/kernel_debugger.h>
#include <base/internal/ipc_server.h>

/* seL4 includes */
#include <sel4/sel4.h>

using namespace Genode;


/**
 * Message-register definitions
 */
enum {
	MR_IDX_EXC_CODE = 0,
	MR_IDX_NUM_CAPS = 1,
	MR_IDX_CAPS     = 2,
	MR_IDX_DATA     = MR_IDX_CAPS + Msgbuf_base::MAX_CAPS_PER_MSG,
};


/**
 * Return reference to receive selector of the calling thread
 */
static unsigned &rcv_sel()
{
	/*
	 * When the function is called at the very early initialization phase, we
	 * cannot access Thread::myself()->native_thread() because the
	 * Thread object of the main thread does not exist yet. During this
	 * phase, we return a reference to the 'main_rcv_sel' variable.
	 */
	if (Thread::myself()) {
		return Thread::myself()->native_thread().rcv_sel;
	}

	static unsigned main_rcv_sel = Capability_space::alloc_rcv_sel();
	return main_rcv_sel;
}


/**
 * Convert Genode::Msgbuf_base content into seL4 message
 *
 * \param msg  source message buffer
 */
static seL4_MessageInfo_t new_seL4_message(Msgbuf_base const &msg)
{
	/*
	 * Supply capabilities to kernel IPC message
	 */
	seL4_SetMR(MR_IDX_NUM_CAPS, msg.used_caps());
	size_t sel4_sel_cnt = 0;
	for (size_t i = 0; i < msg.used_caps(); i++) {

		Native_capability const &cap = msg.cap(i);

		if (cap.valid()) {
			Capability_space::Ipc_cap_data const ipc_cap_data =
				Capability_space::ipc_cap_data(cap);

			seL4_SetMR(MR_IDX_CAPS + i, ipc_cap_data.rpc_obj_key.value());
			seL4_SetCap(sel4_sel_cnt++, ipc_cap_data.sel.value());
		} else {
			seL4_SetMR(MR_IDX_CAPS + i, Rpc_obj_key::INVALID);
		}
	}

	/*
	 * Pad unused capability slots with invalid capabilities to avoid
	 * leakage of any information that happens to be in the IPC buffer.
	 */
	for (size_t i = msg.used_caps(); i < Msgbuf_base::MAX_CAPS_PER_MSG; i++)
		seL4_SetMR(MR_IDX_CAPS + i, Rpc_obj_key::INVALID);

	/*
	 * Allocate and define receive selector
	 */

	if (!rcv_sel())
		rcv_sel() = Capability_space::alloc_rcv_sel();

	/*
	 * Supply data payload
	 */
	size_t const num_data_mwords =
		align_natural(msg.data_size()) / sizeof(umword_t);

	umword_t const *src = (umword_t const *)msg.data();
	for (size_t i = 0; i < num_data_mwords; i++)
		seL4_SetMR(MR_IDX_DATA + i, *src++);

	seL4_MessageInfo_t const msg_info =
		seL4_MessageInfo_new(0, 0, sel4_sel_cnt,
		                     MR_IDX_DATA + num_data_mwords);
	return msg_info;
}


/**
 * Convert seL4 message into Genode::Msgbuf_base
 */
static void decode_seL4_message(seL4_MessageInfo_t const &msg_info,
                                Msgbuf_base &dst_msg)
{
	/*
	 * Extract Genode capabilities from seL4 IPC message
	 */
	dst_msg.reset();
	size_t const num_caps = seL4_GetMR(MR_IDX_NUM_CAPS);
	size_t curr_sel4_cap_idx = 0;

	for (size_t i = 0; i < num_caps; i++) {

		Rpc_obj_key const rpc_obj_key(seL4_GetMR(MR_IDX_CAPS + i));

		/*
		 * Detect passing of invalid capabilities as arguments
		 *
		 * The second condition of the check handles the case where a non-RPC
		 * object capability as passed as RPC argument as done by the
		 * 'Cap_session::alloc' RPC function. Here, the entrypoint capability
		 * is not an RPC-object capability but a raw seL4 endpoint selector.
		 *
		 * XXX Technically, a message may contain one invalid capability
		 *     followed by a valid one. This check would still wrongly regard
		 *     the first capability as a valid one. A better approach would
		 *     be to introduce another state to Rpc_obj_key, which would
		 *     denote a valid capability that is not an RPC-object capability.
		 *     Hence it is meaningless as a key.
		 */
		if (!rpc_obj_key.valid() && seL4_MessageInfo_get_extraCaps(msg_info) == 0) {
			dst_msg.insert(Native_capability());
			continue;
		}

		/*
		 * RPC object key as contained in the message data is valid.
		 */

		unsigned const unwrapped =
			seL4_MessageInfo_get_capsUnwrapped(msg_info) &
			(1 << curr_sel4_cap_idx);

		/* distinguish unwrapped from delegated cap */
		if (unwrapped) {

			/*
			 * Received unwrapped capability
			 *
			 * This means that the capability argument belongs to our endpoint.
			 * So it is already present within the capability space.
			 */

			unsigned const arg_badge =
				seL4_CapData_Badge_get_Badge(seL4_GetBadge(curr_sel4_cap_idx));

			if (arg_badge != rpc_obj_key.value()) {
				PWRN("argument badge (%d) != RPC object key (%d)",
				     arg_badge, rpc_obj_key.value());
			}

			Native_capability arg_cap = Capability_space::lookup(rpc_obj_key);

			dst_msg.insert(arg_cap);

		} else {

			/*
			 * Received delegated capability
			 *
			 * We have either received a capability that is foreign to us,
			 * or an alias for a capability that we already posses. The
			 * latter can happen in the following circumstances:
			 *
			 * - We forwarded a selector that was created by another
			 *   component. We cannot re-identify such a capability when
			 *   handed back because seL4's badge mechanism works only for
			 *   capabilities belonging to the IPC destination endpoint.
			 *
			 * - We received a selector on the IPC reply path, where seL4's
			 *   badge mechanism is not in effect.
			 */

			bool const delegated = seL4_MessageInfo_get_extraCaps(msg_info);

			ASSERT(delegated);

			Native_capability arg_cap = Capability_space::lookup(rpc_obj_key);

			if (arg_cap.valid()) {

				/*
				 * Discard the received selector and keep using the already
				 * present one.
				 *
				 * XXX We'd need to find out if both the received and the
				 *     looked-up selector refer to the same endpoint.
				 *      Unfortunaltely, seL4 lacks such a comparison operation.
				 */

				Capability_space::reset_sel(rcv_sel());

				dst_msg.insert(arg_cap);

			} else {

				Capability_space::Ipc_cap_data const
					ipc_cap_data(rpc_obj_key, rcv_sel());

				dst_msg.insert(Capability_space::import(ipc_cap_data));

				/*
				 * Since we keep using the received selector, we need to
				 * allocate a fresh one for the next incoming delegation.
				 */
				rcv_sel() = Capability_space::alloc_rcv_sel();
			}
		}
		curr_sel4_cap_idx++;
	}

	/*
	 * Extract message data payload
	 */

	size_t const num_msg_words = seL4_MessageInfo_get_length(msg_info);

	/* detect malformed message with too small header */
	if (num_msg_words < MR_IDX_DATA)
		return;

	/* copy data payload */
	size_t const max_words      = dst_msg.capacity()/sizeof(umword_t);
	size_t const num_data_words = min(num_msg_words - MR_IDX_DATA, max_words);

	umword_t *dst = (umword_t *)dst_msg.data();
	for (size_t i = 0; i < num_data_words; i++)
		*dst++ = seL4_GetMR(MR_IDX_DATA + i);

	dst_msg.data_size(num_data_words*sizeof(umword_t));
}


/****************
 ** IPC client **
 ****************/

Rpc_exception_code Genode::ipc_call(Native_capability dst,
                                    Msgbuf_base &snd_msg, Msgbuf_base &rcv_msg,
                                    size_t)
{
	if (!dst.valid()) {
		PERR("Trying to invoke an invalid capability, stop.");
		kernel_debugger_panic("IPC destination is invalid");
	}

	if (!rcv_sel())
		rcv_sel() = Capability_space::alloc_rcv_sel();

	seL4_MessageInfo_t const request_msg_info = new_seL4_message(snd_msg);

	unsigned const dst_sel = Capability_space::ipc_cap_data(dst).sel.value();

	seL4_MessageInfo_t const reply_msg_info =
		seL4_Call(dst_sel, request_msg_info);

	decode_seL4_message(reply_msg_info, rcv_msg);

	return Rpc_exception_code(seL4_GetMR(MR_IDX_EXC_CODE));
}


/****************
 ** IPC server **
 ****************/

void Genode::ipc_reply(Native_capability caller, Rpc_exception_code exc,
                       Msgbuf_base &snd_msg)
{
	ASSERT(false);
}


Genode::Rpc_request Genode::ipc_reply_wait(Reply_capability const &last_caller,
                                           Rpc_exception_code      exc,
                                           Msgbuf_base            &reply_msg,
                                           Msgbuf_base            &request_msg)
{
	seL4_Word badge = 0;

	if (exc.value == Rpc_exception_code::INVALID_OBJECT) {

		seL4_MessageInfo_t const request_msg_info =
			seL4_Recv(Thread::myself()->native_thread().ep_sel, &badge);

		decode_seL4_message(request_msg_info, request_msg);

	} else {

		seL4_MessageInfo_t const reply_msg_info = new_seL4_message(reply_msg);

		seL4_SetMR(MR_IDX_EXC_CODE, exc.value);

		seL4_MessageInfo_t const request_msg_info =
			seL4_ReplyRecv(Thread::myself()->native_thread().ep_sel,
			               reply_msg_info, &badge);

		decode_seL4_message(request_msg_info, request_msg);
	}

	return Rpc_request(Native_capability(), badge);
}


Ipc_server::Ipc_server()
:
	Native_capability(Capability_space::create_ep_cap(*Thread::myself()))
{ }


Ipc_server::~Ipc_server() { }
