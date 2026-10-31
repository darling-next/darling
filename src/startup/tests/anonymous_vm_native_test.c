#define _GNU_SOURCE
#include "../mldr/anonymous_vm.h"
/* Assertions are the executable oracle, including in Release builds. */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int operate(struct darling_vm_request request, struct darling_vm_result* result) {
	struct darling_vm_result discard;
	return mldr_vm_operation(&request, result ? result : &discard);
}
static struct darling_vm_result query(uint64_t address) {
	struct darling_vm_result result;
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_REGION, .address = address }, &result));
	return result;
}
static void* worker(void* unused) {
	(void)unused;
	for (int i = 0; i < 64; ++i) {
		struct darling_vm_result r;
		assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_ANONYMOUS_MAP,
			.size = 12288, .flags = 1, .mask = 65535, .protection = 3, .maximum = 3,
			.inheritance = 1, .fd = -1 }, &r));
		assert(!(r.address & 65535));
		assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_MACH_PROTECT,
			.address = r.address + 4096, .size = 4096, .protection = 1, .set_maximum = 1 }, NULL));
		assert(operate((struct darling_vm_request){ .operation = DARLING_VM_PROTECT,
			.address = r.address + 4096, .size = 4096, .protection = 3 }, NULL) == -EACCES);
		assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_UNMAP,
			.address = r.address, .size = r.size }, NULL));
	}
	return NULL;
}
static void protect_copy_contract(void) {
	struct darling_vm_result r;
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_ANONYMOUS_MAP,
		.size = 4096, .flags = 1, .protection = 1, .maximum = 1,
		.inheritance = 2, .fd = -1 }, &r));
	uint64_t address = r.address;
	struct darling_vm_request protect = { .operation = DARLING_VM_MACH_PROTECT,
		.address = address, .size = 4096, .protection = 0x15 };
	assert(operate(protect, NULL) == -EACCES);
	protect.protection = 0x13;
	assert(!operate(protect, NULL));
	r = query(address);
	assert(r.protection == 3 && r.maximum == 3 && r.inheritance == 2 && !r.shared);
	*(volatile char*)(uintptr_t)address = 42;
	protect.protection = 0x11;
	protect.set_maximum = 1;
	assert(!operate(protect, NULL));
	r = query(address);
	assert(r.protection == 1 && r.maximum == 1 && r.inheritance == 2);
	protect.operation = DARLING_VM_PROTECT;
	protect.protection = 0x13;
	assert(operate(protect, NULL) == -EINVAL);
	assert(*(volatile char*)(uintptr_t)address == 42);
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_UNMAP,
		.address = address, .size = 4096 }, NULL));
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_MMAP,
		.size = 4096, .protection = 3, .flags = MAP_SHARED | MAP_ANONYMOUS,
		.fd = -1 }, &r));
	address = r.address;
	struct darling_vm_result before_copy = query(address);
	protect = (struct darling_vm_request){ .operation = DARLING_VM_MACH_PROTECT,
		.address = address, .size = 4096, .protection = 0x11, .set_maximum = 1 };
	assert(operate(protect, NULL) == -ENOTSUP);
	r = query(address);
	assert(r.protection == before_copy.protection && r.maximum == before_copy.maximum);
	assert(r.inheritance == before_copy.inheritance && r.shared == before_copy.shared);
	*(volatile char*)(uintptr_t)address = 71;
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_UNMAP,
		.address = address, .size = 4096 }, NULL));
}

int main(void) {
	protect_copy_contract();
	struct darling_vm_result r;
	struct darling_vm_request map = { .operation = DARLING_VM_ANONYMOUS_MAP,
		.size = 1u << 24, .mask = (1u << 24) - 1, .flags = 1,
		.protection = 3, .maximum = 3, .inheritance = 2, .fd = -1 };
	struct darling_vm_request unsupported = map;
	unsupported.inheritance = 0; /* SHARE has no anonymous native backend. */
	assert(operate(unsupported, NULL) == -ENOTSUP);
	unsupported = map;
	unsupported.flags |= 2; /* PURGABLE is not an ordinary allocation hint. */
	assert(operate(unsupported, NULL) == -EINVAL);
	unsupported = map;
	unsupported.protection = 7;
	assert(operate(unsupported, NULL) == -EINVAL);
	unsupported = map;
	unsupported.size = UINT64_MAX;
	assert(operate(unsupported, NULL) == -EINVAL);
	assert(!operate(map, &r));
	uint64_t base = r.address;
	assert(base && !(base & map.mask));
	*(volatile char*)(uintptr_t)base = 42;
	map.flags = 0; map.address = base; map.size = 4096; map.mask = 0;
	assert(operate(map, NULL) == -EEXIST);
	assert(*(volatile char*)(uintptr_t)base == 42);
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_MACH_PROTECT,
		.address = base + 4096, .size = 4096, .protection = 1, .set_maximum = 1 }, NULL));
	/* Coalesce Linux current rights while preserving distinct maximum rights. */
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_PROTECT,
		.address = base, .size = 3 * 4096, .protection = 1 }, NULL));
	r = query(base + 4096);
	assert(r.address == base + 4096 && r.size == 4096 && r.maximum == 1 && r.protection == 1 && r.inheritance == 2);
	r = query(base); assert(r.size == 4096 && r.maximum == 3);
	assert(operate((struct darling_vm_request){ .operation = DARLING_VM_MACH_PROTECT,
		.address = base + 4096, .size = 4096, .protection = 3, .set_maximum = 1 }, NULL) == -EACCES);
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_INHERIT,
		.address = base, .size = 4096, .inheritance = 1 }, NULL));
	mldr_vm_prefork();
	pid_t child = syscall(SYS_fork);
	assert(child >= 0);
	if (!child) {
		mldr_vm_postfork_child();
		unsigned char vec;
		if (*(volatile char*)(uintptr_t)base != 42) _exit(2);
		errno = 0;
		if (mincore((void*)(uintptr_t)(base + 4096), 4096, &vec) != -1 || errno != ENOMEM) _exit(3);
		struct darling_vm_result fresh;
		if (operate((struct darling_vm_request){ .operation = DARLING_VM_MMAP,
			.address = base + 4096, .size = 4096, .flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.protection = 3, .fd = -1 }, &fresh)) _exit(4);
		fresh = query(base + 4096);
		if (fresh.maximum != 7 || fresh.inheritance != 1) _exit(5);
		_exit(0);
	}
	mldr_vm_postfork_parent();
	int status; assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_UNMAP,
		.address = base + 4096, .size = 4096 }, NULL));
	assert(operate((struct darling_vm_request){ .operation = DARLING_VM_MACH_PROTECT,
		.address = base, .size = 3 * 4096, .protection = 1 }, NULL) == -ENOMEM);
	assert(!operate((struct darling_vm_request){ .operation = DARLING_VM_UNMAP,
		.address = base, .size = 1u << 24 }, NULL));
	pthread_t threads[4];
	for (int i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, worker, NULL));
	for (int i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
	puts("ANONYMOUS_VM_NATIVE_PASS max=1 split=1 collision=1 fork_none=1 concurrent=1");
	return 0;
}
