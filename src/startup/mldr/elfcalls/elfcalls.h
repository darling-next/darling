#ifndef _ELFCALLS_H_
#define _ELFCALLS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// perf#30 REMOVAL STEP 3c: there is no per-thread RPC fd to guard any more, so the callback pair that registered and
// removed one is gone from the ABI. What remains guarded is the PROCESS socket and the process lifetime pipe, both
// registered by the kernel image directly (guard:init, fork.c).
struct darling_thread_create_callbacks {
	unsigned int (*thread_self_trap)(void);
	void (*thread_set_tsd_base)(void*, int);
};

typedef const struct darling_thread_create_callbacks* darling_thread_create_callbacks_t;

struct elf_calls
{
	// ELF dynamic loader access
	void* (*dlopen)(const char* name);
	int (*dlclose)(void* lib);
	void* (*dlsym)(void* lib, const char* name);
	char* (*dlerror)(void);

	// pthread wrapping
	void* (*darling_thread_create)(unsigned long stack_size, unsigned long pthobj_size,
                void* entry_point, uintptr_t arg3,
                uintptr_t arg4, uintptr_t arg5, uintptr_t arg6,
                darling_thread_create_callbacks_t callbacks, void* dthread);
	int (*darling_thread_terminate)(void* stackaddr,
				unsigned long freesize, unsigned long pthobj_size);

	// this returns the address of the main thread's stack
	void* (*darling_thread_get_stack)(void);

	// The same as above, except they abort() in case of failure
	void* (*dlopen_fatal)(const char* name);
	int (*dlclose_fatal)(void* lib);
	void* (*dlsym_fatal)(void* lib, const char* name);

	// POSIX semaphore APIs
	int (*get_errno)(void);
	int* (*sem_open)(const char* name, int oflag, unsigned short mode, unsigned int value);
	int (*sem_wait)(int* sem);
	int (*sem_trywait)(int* sem);
	int (*sem_post)(int* sem);
	int (*sem_close)(int* sem);
	int (*sem_unlink)(const char* name);

	// POSIX SHM APIs
	int (*shm_open)(const char* name, int oflag, unsigned short mode);
	int (*shm_unlink)(const char* name);

	void (*exit)(int ec);

	// Memory allocation
	void* (*malloc)(size_t size);
	void (*free)(void *ptr);
	void* (*realloc)(void *ptr, size_t size);

	// POSIX sysconf
	long (*sysconf)(int name);

	// mldr process state
	void (*postfork_child)(void);

	// darlingserver RPC info
	const void* (*dserver_socket_address)(void);
	int (*dserver_process_socket)(void);
	void (*dserver_close_socket)(int socket);

	// darlingserver process lifetime pipe info
	int (*dserver_get_process_lifetime_pipe)(void);
	int (*dserver_process_lifetime_pipe_refresh)(void);
	void (*dserver_close_process_lifetime_pipe)(int fd);

	// Loader-owned ring descriptors, shared across guest images.
	int (*dserver_adopt_ring_fd)(int fd);
	bool (*dserver_fd_is_internal)(int fd);
	void (*prefork_prepare)(void);
	void (*postfork_parent)(void);

	// perf#27 (lane lifecycle): the counterpart of adopt -- the guest lane that owned the descriptor is
	// gone, so the loader must forget the number as well as close it. APPENDED, never inserted: this
	// struct is the loader/guest interface and every existing field must keep its offset, or a stale
	// image in the prefix reads a different function at the same slot (which is exactly how an inserted
	// field broke the dyld image's guard calls the first time this was added).
	void (*dserver_release_ring_fd)(int fd);

	// perf#28 (ONE doorbell): adopt the PROCESS-WIDE Ring wake descriptor. The server sends a dup of
	// its single doorbell on every lane attach; the loader keeps the first and closes every later dup,
	// so one Linux guest process (all its lanes, both guest images) costs exactly one Ring wake fd.
	// APPENDED for the same reason as dserver_release_ring_fd above: every existing field must keep its
	// offset or a stale image reads the wrong function at the same slot.
	int (*dserver_ring_doorbell)(int fd);

	// perf#30 (PROCESS-GLOBAL LANE DIRECTORY): the loader-owned record array that gives ONE logical Ring
	// lane incarnation per Linux host tid to every guest image. APPENDED, like every field above: the
	// struct is the loader/guest ABI, so an inserted field would shift every later slot and let a stale
	// image read a different function.
	void* (*dserver_ring_lane_registry)(void);
	int   (*dserver_ring_lane_slots)(void);


	// perf#30 FD-COURIER (PRODUCT): the loader-owned abstract address of the ONE process-scoped
	// SCM_RIGHTS descriptor channel. APPENDED at the end, like every field above: the struct is the
	// loader/guest ABI, so inserting a field would shift every later slot and let a stale image read a
	// different function. The address is the loader's because the loader owns process-level transport
	// state -- the same ownership the lane directory above expresses.
	const void* (*dserver_fd_courier_address)(void);

	// perf#30 FD-COURIER: this PROCESS's incarnation identity, asked of the loader so that every image in
	// the process reports the SAME value. Per-image values are not usable here: a process runs several
	// images (the emulation dylib, dyld, ...), each with its own copy of any static, and the server
	// rejects a second, different generation for a pid as a stale incarnation. APPENDED (see above).
	uint64_t (*dserver_process_generation)(void);

	// perf#30 FD-COURIER: the connected descriptor itself, owned by the loader. The channel is ONE
	// connection per Linux process, so the images in that process must share it rather than each opening
	// its own -- only then is a process's identity (and its generation) single-valued. APPENDED.
	int (*dserver_fd_courier_socket)(void);

	// perf#30 FD-COURIER: drop the process's courier connection (a fork child must not write onto its
	// parent's connection). The IMAGE cannot do this by closing the number it happens to hold: the
	// connection is the loader's, so the loader must be the one that forgets it. APPENDED.
	void (*dserver_fd_courier_reset)(void);

	// perf#30 PROCESS-CONTROL PLANE: the process's management page, created once by the loader. Every
	// image asks for it here rather than creating one: an image-local page would be a SECOND region for
	// the same process, and the server keeps one per pid -- the first would be unmapped underneath its
	// owner. APPENDED.
	void* (*dserver_process_control_page)(void);

};

#endif
