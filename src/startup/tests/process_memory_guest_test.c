/* Compile and execute in Darling: real Mach copyin/copyout across an
 * inaccessible page must not return success with an incomplete buffer. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition) do { if (!(condition)) { \
	fprintf(stderr, "PROCESS_MEMORY_GUEST_FAIL line=%d expression=%s errno=%d\n", \
		__LINE__, #condition, errno); exit(1); } } while (0)

static void filled(const unsigned char* data, size_t size, unsigned char value) {
	for (size_t i = 0; i < size; ++i) REQUIRE(data[i] == value);
}

int main(void) {
	const size_t page = (size_t)getpagesize();
	unsigned char* source = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	unsigned char* destination = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	REQUIRE(source != MAP_FAILED && destination != MAP_FAILED);
	memset(source, 0x35, 2 * page);
	memset(destination, 0x19, 2 * page);
	mach_vm_size_t copied = 0;
	REQUIRE(mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)source,
		page, (mach_vm_address_t)(uintptr_t)destination, &copied) == KERN_SUCCESS);
	REQUIRE(copied == page);
	filled(destination, page, 0x35);
	REQUIRE(mprotect(source + page, page, PROT_NONE) == 0);
	memset(destination, 0x19, 2 * page);
	copied = 0;
	kern_return_t read_status = mach_vm_read_overwrite(mach_task_self(),
		(mach_vm_address_t)(uintptr_t)source, 2 * page,
		(mach_vm_address_t)(uintptr_t)destination, &copied);
	printf("PROCESS_MEMORY_READ requested=%zu status=%d reported=%llu\n",
		2 * page, read_status, (unsigned long long)copied);
	REQUIRE(read_status != KERN_SUCCESS);
	/* Failed source copy must not expose a server staging buffer. */
	filled(destination, 2 * page, 0x19);
	REQUIRE(mprotect(source + page, page, PROT_READ | PROT_WRITE) == 0);
	REQUIRE(mprotect(destination + page, page, PROT_NONE) == 0);
	copied = 0;
	kern_return_t overwrite_status = mach_vm_read_overwrite(mach_task_self(),
		(mach_vm_address_t)(uintptr_t)source, 2 * page,
		(mach_vm_address_t)(uintptr_t)destination, &copied);
	printf("PROCESS_MEMORY_OVERWRITE requested=%zu status=%d reported=%llu\n",
		2 * page, overwrite_status, (unsigned long long)copied);
	REQUIRE(overwrite_status != KERN_SUCCESS);
	/* A failed destination copy may already have written its accessible prefix. */
	filled(destination, page, 0x35);
	memset(source, 0x73, 2 * page);
	kern_return_t write_status = mach_vm_write(mach_task_self(),
		(mach_vm_address_t)(uintptr_t)destination,
		(vm_offset_t)(uintptr_t)source, (mach_msg_type_number_t)(2 * page));
	printf("PROCESS_MEMORY_WRITE requested=%zu status=%d\n", 2 * page, write_status);
	REQUIRE(write_status != KERN_SUCCESS);
	filled(destination, page, 0x73);
	REQUIRE(munmap(source, 2 * page) == 0);
	REQUIRE(munmap(destination, 2 * page) == 0);
	puts("PROCESS_MEMORY_GUEST_PASS exact_read=1 short_read_rejected=1 short_overwrite_rejected=1 short_write_rejected=1");
	return 0;
}
