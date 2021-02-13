/*
 * \brief  Framebuffer session interface
 * \author Norman Feske
 * \date   2006-07-10
 */

/*
 * Copyright (C) 2006-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _INCLUDE__FRAMEBUFFER_SESSION__FRAMEBUFFER_SESSION_H_
#define _INCLUDE__FRAMEBUFFER_SESSION__FRAMEBUFFER_SESSION_H_

#include <base/output.h>
#include <base/signal.h>
#include <dataspace/capability.h>
#include <session/session.h>
#include <os/surface.h>

namespace Framebuffer {

	struct Mode;
	struct Session;
	struct Session_client;

	using Area = Genode::Surface_base::Area;
}


/**
 * Framebuffer mode info as returned by 'Framebuffer::Session::mode()'
 */
struct Framebuffer::Mode
{
	Area area;

	Genode::size_t bytes_per_pixel() const { return 4; }

	void print(Genode::Output &out) const { Genode::print(out, area); }
};


struct Framebuffer::Session : Genode::Session
{
	/**
	 * \noapi
	 */
	static const char *service_name() { return "Framebuffer"; }

	/*
	 * A framebuffer session consumes a dataspace capability for the server's
	 * session-object allocation, a dataspace capability for the framebuffer
	 * dataspace, and its session capability.
	 */
	enum { CAP_QUOTA = 3 };

	typedef Session_client Client;

	virtual ~Session() { }

	/**
	 * Request dataspace representing the logical frame buffer
	 *
	 * By calling this method, the framebuffer client enables the server
	 * to reallocate the framebuffer dataspace (e.g., on mode changes).
	 * Hence, prior calling this method, the client should make sure to
	 * have detached the previously requested dataspace from its local
	 * address space.
	 */
	virtual Genode::Dataspace_capability dataspace() = 0;

	/**
	 * Request display-mode properties of the framebuffer ready to be
	 * obtained via the 'dataspace()' method
	 */
	virtual Mode mode() const = 0;

	/**
	 * Register signal handler to be notified on mode changes
	 *
	 * The framebuffer server may support changing the display mode on the
	 * fly. For example, a virtual framebuffer presented in a window may
	 * get resized according to the window dimensions. By installing a
	 * signal handler for mode changes, the framebuffer client can respond
	 * to such changes. The new mode can be obtained using the 'mode()'
	 * method. However, from the client's perspective, the original mode
	 * stays in effect until the it calls 'dataspace()' again.
	 */
	virtual void mode_sigh(Genode::Signal_context_capability sigh) = 0;

	/**
	 * Flush specified pixel region
	 *
	 * \param x,y,w,h  region to be updated on physical frame buffer
	 */
	virtual void refresh(int x, int y, int w, int h) = 0;

	/**
	 * Register signal handler for refresh synchronization
	 */
	virtual void sync_sigh(Genode::Signal_context_capability) = 0;


	/*********************
	 ** RPC declaration **
	 *********************/

	GENODE_RPC(Rpc_dataspace, Genode::Dataspace_capability, dataspace);
	GENODE_RPC(Rpc_mode, Mode, mode);
	GENODE_RPC(Rpc_refresh, void, refresh, int, int, int, int);
	GENODE_RPC(Rpc_mode_sigh, void, mode_sigh, Genode::Signal_context_capability);
	GENODE_RPC(Rpc_sync_sigh, void, sync_sigh, Genode::Signal_context_capability);

	GENODE_RPC_INTERFACE(Rpc_dataspace, Rpc_mode, Rpc_mode_sigh, Rpc_refresh,
	                     Rpc_sync_sigh);
};

#endif /* _INCLUDE__FRAMEBUFFER_SESSION__FRAMEBUFFER_SESSION_H_ */
