/*
 * Copyright (c) 2007, 2008, 2011, 2012 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <TargetConditionals.h>	// for TARGET_OS_EMBEDDED

#include <pthread.h>
#include <pthread/private.h>
#include <_libkernel_init.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <TargetConditionals.h>

struct ProgramVars; /* forward reference */

// system library initialisers
extern void bootstrap_init(void);		// from liblaunch.dylib
extern void mach_init(void);			// from libsystem_kernel.dylib
extern void __libplatform_init(void *future_use, const char *envp[], const char *apple[], const struct ProgramVars *vars);
extern void __pthread_init(const struct _libpthread_functions *libpthread_funcs, const char *envp[], const char *apple[], const struct ProgramVars *vars);	// from libsystem_pthread.dylib
extern void __libc_init(const struct ProgramVars *vars, void (*atfork_prepare)(void), void (*atfork_parent)(void), void (*atfork_child)(void), const char *apple[]);	// from libsystem_c.dylib
extern void __malloc_init(const char *apple[]); // from libsystem_malloc.dylib
extern void __keymgr_initializer(void);		// from libkeymgr.dylib
extern void _dyld_initializer(void);		// from libdyld.dylib
extern void libdispatch_init(void);		// from libdispatch.dylib
extern void _libxpc_initializer(void);		// from libxpc.dylib

// signal malloc stack logging that initialisation has finished
extern void __stack_logging_early_finished(void); // form libsystem_c.dylib

// system library atfork handlers
extern void _pthread_fork_prepare(void);
extern void _pthread_fork_parent(void);
extern void _pthread_fork_child(void);
extern void _pthread_fork_child_postinit(void);

extern void dispatch_atfork_prepare(void);
extern void dispatch_atfork_parent(void);
extern void dispatch_atfork_child(void);

extern void _malloc_fork_prepare(void);
extern void _malloc_fork_parent(void);
extern void _malloc_fork_child(void);

extern void _mach_fork_child(void);
extern void _libc_fork_child(void);
extern void _notify_fork_child(void);
extern void _dyld_fork_child(void);
extern void xpc_atfork_prepare(void);
extern void xpc_atfork_parent(void);
extern void xpc_atfork_child(void);
extern void _libSC_info_fork_prepare(void);
extern void _libSC_info_fork_parent(void);
extern void _libSC_info_fork_child(void);
extern void _asl_fork_child(void);

// advance decls for below;
void libSystem_atfork_prepare(void);
void libSystem_atfork_parent(void);
void libSystem_atfork_child(void);

void _pthread_exit_if_canceled(int);

/*
 * libsyscall_initializer() initializes all of libSystem.dylib <rdar://problem/4892197>
 */
static __attribute__((constructor))
void libSystem_initializer(int argc, const char* argv[], const char* envp[], const char* apple[], const struct ProgramVars* vars)
{
	static const struct _libkernel_functions libkernel_funcs = {
		.version = 1,
		.dlsym = dlsym,
		.malloc = malloc,
		.free = free,
		.realloc = realloc,
		._pthread_exit_if_canceled = _pthread_exit_if_canceled,
	};

	static const struct _libpthread_functions libpthread_funcs = {
		.version = 1,
		.exit = exit,
	};

	__libkernel_init(&libkernel_funcs, envp, apple, vars);

	bootstrap_init();
	__libplatform_init(NULL, envp, apple, vars);

	__pthread_init(&libpthread_funcs, envp, apple, vars);
	__libc_init(vars, libSystem_atfork_prepare, libSystem_atfork_parent, libSystem_atfork_child, apple);

	// TODO: Move __malloc_init before __libc_init after breaking malloc's upward link to Libc
	__malloc_init(apple);

	_dyld_initializer();
	libdispatch_init();
	_libxpc_initializer();

	__stack_logging_early_finished();

	/* <rdar://problem/11588042>
	 * C99 standard has the following in section 7.5(3):
	 * "The value of errno is zero at program startup, but is never set
	 * to zero by any library function."
	 */
	errno = 0;
}

/*
 * libSystem_atfork_{prepare,parent,child}() are called by libc when we fork, then we deal with running fork handlers
 * for everyone else.
 */
void libSystem_atfork_prepare(void)
{
	_libSC_info_fork_prepare();
	xpc_atfork_prepare();
	dispatch_atfork_prepare();
	_pthread_fork_prepare();
	_malloc_fork_prepare();
}

void libSystem_atfork_parent(void)
{
	_malloc_fork_parent();
	_pthread_fork_parent();
	dispatch_atfork_parent();
	xpc_atfork_parent();
	_libSC_info_fork_parent();
}

void libSystem_atfork_child(void)
{
	_dyld_fork_child();
	_pthread_fork_child();
	_malloc_fork_child();
	dispatch_atfork_child();
	
	bootstrap_init();
	_mach_fork_child();
	_libc_fork_child();
	_asl_fork_child();
	_notify_fork_child();
	xpc_atfork_child();
	_libSC_info_fork_child();

	_pthread_fork_child_postinit();
}

/*  
 *  Old crt1.o glue used to call through mach_init_routine which was used to initialize libSystem.
 *  LibSystem now auto-initializes but mach_init_routine is left for binary compatibility.
 */
static void mach_init_old(void) {}
void (*mach_init_routine)(void) = &mach_init_old;

/*
 *	This __crashreporter_info__ symbol is for all non-dylib parts of libSystem.
 */
const char *__crashreporter_info__;
asm (".desc __crashreporter_info__, 0x10");
