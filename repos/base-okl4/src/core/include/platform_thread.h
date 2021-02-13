/*
 * \brief   OKL4 thread facility
 * \author  Norman Feske
 * \date    2009-03-31
 */

/*
 * Copyright (C) 2009-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__PLATFORM_THREAD_H_
#define _CORE__INCLUDE__PLATFORM_THREAD_H_

/* Genode includes */
#include <base/thread_state.h>
#include <base/trace/types.h>

/* core includes */
#include <pager.h>
#include <platform_pd.h>
#include <assertion.h>

namespace Genode {

	class Platform_pd;
	class Platform_thread;
}


class Genode::Platform_thread
{
	private:

		/*
		 * Noncopyable
		 */
		Platform_thread(Platform_thread const &);
		Platform_thread &operator = (Platform_thread const &);

		int _thread_id = THREAD_INVALID;    /* plain thread number */

		Okl4::L4_ThreadId_t _l4_thread_id;  /* L4 thread ID */

		char          _name[32];            /* thread name that will be
		                                       registered at the kernel
		                                       debugger */
		Platform_pd  *_platform_pd;         /* protection domain thread
		                                       is bound to */
		unsigned      _priority;            /* thread priority */
		Pager_object *_pager;

	public:

		enum { THREAD_INVALID   = -1 };   /* invalid thread number */
		enum { DEFAULT_PRIORITY = 128 };

		/**
		 * Constructor
		 */
		Platform_thread(size_t, const char *name,
		                unsigned priority,
		                Affinity::Location,
		                addr_t utcb);

		/**
		 * Constructor used for core-internal threads
		 */
		Platform_thread(char const *name)
		: Platform_thread(0, name, 0, Affinity::Location(), 0) { }

		/**
		 * Destructor
		 */
		~Platform_thread();

		/**
		 * Start thread
		 *
		 * \param ip      instruction pointer to start at
		 * \param sp      stack pointer to use
		 * \param cpu_no  target cpu
		 *
		 * \retval  0  successful
		 * \retval -1  thread could not be started
		 */
		int start(void *ip, void *sp, unsigned int cpu_no = 0);

		/**
		 * Pause this thread
		 */
		void pause();

		/**
		 * Enable/disable single stepping
		 */
		void single_step(bool) { }

		/**
		 * Resume this thread
		 */
		void resume();

		/**
		 * This thread is about to be bound
		 *
		 * \param thread_id     local thread ID
		 * \param l4_thread_id  final L4 thread ID
		 * \param pd            platform pd, thread is bound to
		 */
		void bind(int thread_id, Okl4::L4_ThreadId_t l4_thread_id,
		          Platform_pd &pd);

		/**
		 * Unbind this thread
		 */
		void unbind();

		/**
		 * Override thread state with 's'
		 *
		 * \throw Cpu_session::State_access_failed
		 */
		void state(Thread_state s);

		/**
		 * Read thread state
		 */
		Thread_state state();

		/************************
		 ** Accessor functions **
		 ************************/

		/**
		 * Return/set pager
		 */
		Pager_object &pager() const
		{
			if (_pager)
				return *_pager;

			ASSERT_NEVER_CALLED;
		}

		void pager(Pager_object &pager) { _pager = &pager; }

		/**
		 * Get the 'Platform_pd' object this thread belongs to
		 */
		Platform_pd* pd() { return _platform_pd; }

		/**
		 * Return identification of thread when faulting
		 */
		unsigned long pager_object_badge() const;

		/**
		 * Set the executing CPU for this thread
		 */
		void affinity(Affinity::Location) { }

		/**
		 * Request the affinity of this thread
		 */
		Affinity::Location affinity() const { return Affinity::Location(); }

		/**
		 * Set CPU quota of the thread
		 */
		void quota(size_t) { /* not supported */ }

		/**
		 * Return execution time consumed by the thread
		 */
		Trace::Execution_time execution_time() const { return { 0, 0 }; }


		/*****************************
		 ** OKL4-specific Accessors **
		 *****************************/

		int                 thread_id()        const { return _thread_id; }
		Okl4::L4_ThreadId_t native_thread_id() const { return _l4_thread_id; }
		const char         *name()             const { return _name; }

		void set_l4_thread_id(Okl4::L4_ThreadId_t id) { _l4_thread_id = id; }
};

#endif /* _CORE__INCLUDE__PLATFORM_THREAD_H_ */
