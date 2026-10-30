/* Compile and execute inside the selected Darling installation. This exercises
 * real Mach RPC, local traps, BSD protection and native fork, not a model. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define REQUIRE(condition) do { if (!(condition)) { \
	fprintf(stderr, "ANONYMOUS_VM_FAIL line=%d expression=%s errno=%d\n", __LINE__, #condition, errno); \
	exit(1); } } while (0)

static vm_region_basic_info_data_64_t attributes(mach_vm_address_t address, mach_vm_size_t* size) {
	vm_region_basic_info_data_64_t info = {0};
	mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
	mach_port_t object = MACH_PORT_NULL;
	mach_vm_address_t actual = address;
	REQUIRE(mach_vm_region(mach_task_self(), &actual, size, VM_REGION_BASIC_INFO_64,
		(vm_region_info_t)&info, &count, &object) == KERN_SUCCESS);
	REQUIRE(actual <= address && address < actual + *size);
	if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
	return info;
}

int main(void) {
	const mach_vm_size_t page = (mach_vm_size_t)getpagesize();
	const mach_vm_size_t alignment = 16 * 1024 * 1024;
	mach_vm_address_t address = 0;
	/* Exact JSC shape: RW maximum (not ALL), NONE inheritance, large mask. */
	REQUIRE(mach_vm_map(mach_task_self(), &address, alignment, alignment - 1,
		VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL, 0, FALSE,
		VM_PROT_READ | VM_PROT_WRITE, VM_PROT_READ | VM_PROT_WRITE,
		VM_INHERIT_NONE) == KERN_SUCCESS);
	REQUIRE(address && !(address & (alignment - 1)));
	*(volatile unsigned char*)(uintptr_t)address = 0x35;
	mach_vm_size_t region_size;
	vm_region_basic_info_data_64_t info = attributes(address, &region_size);
	REQUIRE(info.protection == (VM_PROT_READ | VM_PROT_WRITE));
	REQUIRE(info.max_protection == (VM_PROT_READ | VM_PROT_WRITE));
	REQUIRE(info.inheritance == VM_INHERIT_NONE);

	/* Fixed without OVERWRITE must preserve both mapping and contents. */
	mach_vm_address_t collision = address;
	REQUIRE(mach_vm_map(mach_task_self(), &collision, page, 0, VM_FLAGS_FIXED,
		MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_READ, VM_PROT_READ,
		VM_INHERIT_COPY) == KERN_NO_SPACE);
	REQUIRE(*(volatile unsigned char*)(uintptr_t)address == 0x35);
	REQUIRE(mprotect((void*)(uintptr_t)address, page, PROT_READ | PROT_EXEC) == -1);
	REQUIRE(errno == EACCES);
	REQUIRE(mach_vm_protect(mach_task_self(), address, page, FALSE,
		VM_PROT_READ | VM_PROT_EXECUTE) == KERN_PROTECTION_FAILURE);

	/* Lowering only the middle page splits max metadata even if Linux VMAs
	 * subsequently coalesce. Current permission is clamped as well. */
	REQUIRE(mach_vm_protect(mach_task_self(), address + page, page, TRUE,
		VM_PROT_READ) == KERN_SUCCESS);
	REQUIRE(mprotect((void*)(uintptr_t)address, 3 * page, PROT_READ) == 0);
	info = attributes(address + page, &region_size);
	REQUIRE(region_size == page && info.max_protection == VM_PROT_READ);
	REQUIRE(info.protection == VM_PROT_READ && info.inheritance == VM_INHERIT_NONE);
	vm_region_submap_short_info_data_64_t recurse = {0};
	mach_msg_type_number_t recurse_count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
	natural_t depth = 0;
	mach_vm_address_t recurse_address = address + page;
	mach_vm_size_t recurse_size = 0;
	REQUIRE(mach_vm_region_recurse(mach_task_self(), &recurse_address, &recurse_size,
		&depth, (vm_region_recurse_info_t)&recurse, &recurse_count) == KERN_SUCCESS);
	REQUIRE(recurse_address == address + page && recurse_size == page);
	REQUIRE(recurse.max_protection == VM_PROT_READ && recurse.inheritance == VM_INHERIT_NONE);
	REQUIRE(mprotect((void*)(uintptr_t)(address + page), page, PROT_READ | PROT_WRITE) == -1);
	REQUIRE(errno == EACCES);
	info = attributes(address, &region_size);
	REQUIRE(region_size == page && info.max_protection == (VM_PROT_READ | VM_PROT_WRITE));

	/* COPY survives fork independently; NONE must actually be absent, not just
	 * labelled differently. mincore observes absence without deliberately faulting. */
	REQUIRE(mprotect((void*)(uintptr_t)address, page, PROT_READ | PROT_WRITE) == 0);
	REQUIRE(mach_vm_inherit(mach_task_self(), address, page, VM_INHERIT_COPY) == KERN_SUCCESS);
	pid_t child = fork();
	REQUIRE(child >= 0);
	if (!child) {
		unsigned char residency = 0;
		if (*(volatile unsigned char*)(uintptr_t)address != 0x35) _exit(2);
		*(volatile unsigned char*)(uintptr_t)address = 0x71;
		errno = 0;
		if (mincore((void*)(uintptr_t)(address + page), page, (char*)&residency) != -1 || errno != ENOMEM) _exit(3);
		/* Child pruning must permit a new ordinary mapping at the vanished page
		 * without retaining the parent's lowered maximum. */
		void* fresh = mmap((void*)(uintptr_t)(address + page), page,
			PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
		if (fresh == MAP_FAILED || mprotect(fresh, page, PROT_READ | PROT_WRITE)) _exit(4);
		_exit(0);
	}
	int child_status;
	REQUIRE(waitpid(child, &child_status, 0) == child);
	REQUIRE(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	REQUIRE(*(volatile unsigned char*)(uintptr_t)address == 0x35);

	/* Unmap an interior page and overwrite a restricted neighbour. These BSD
	 * mutations must remove stale rights while keeping the remaining intervals. */
	REQUIRE(munmap((void*)(uintptr_t)(address + page), page) == 0);
	REQUIRE(mach_vm_protect(mach_task_self(), address, 3 * page, FALSE,
		VM_PROT_READ) == KERN_INVALID_ADDRESS);
	void* replacement = mmap((void*)(uintptr_t)address, page,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	REQUIRE(replacement == (void*)(uintptr_t)address);
	info = attributes(address, &region_size);
	REQUIRE(info.max_protection == VM_PROT_ALL && info.inheritance == VM_INHERIT_COPY);
	REQUIRE(mach_vm_deallocate(mach_task_self(), address, alignment) == KERN_SUCCESS);
	printf("ANONYMOUS_VM_PASS alignment=%llu max=1 inheritance=1 fork_none=1 lifecycle=1\n",
		(unsigned long long)alignment);
	return 0;
}
