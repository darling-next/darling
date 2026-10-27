/* Exercise the installed libc/libpthread, compiled and run inside Darling.
 * The assembly creates a real Swift ABI extended OS frame; its continuation
 * records and reserved TSD task header follow Apple Libc-1534.100.14. */
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__x86_64__)
#error This fixture requires the x86_64 Swift extended-frame ABI
#endif

typedef size_t (*backtrace_async_fn)(void **, size_t, uint32_t *);
typedef uintptr_t (*decode_frame_fn)(uintptr_t, uintptr_t *);

struct async_context {
	uintptr_t parent;
	uintptr_t resume;
} __attribute__((aligned(16)));

extern size_t darling_test_async_frame(const struct async_context *, void **,
	size_t, uint32_t *, backtrace_async_fn);
extern const char darling_test_async_return[];

/* Preserve the ABI tag until the walker has observed this actual frame. */
__asm__(
	".text\n"
	".p2align 4\n"
	".globl _darling_test_async_frame\n"
	"_darling_test_async_frame:\n"
	"pushq %rbp\n"
	"movq %rsp, %rbp\n"
	"subq $16, %rsp\n"
	"movq %rdi, -8(%rbp)\n"
	"btsq $60, (%rbp)\n"
	"movq %r8, %rax\n"
	"movq %rsi, %rdi\n"
	"movq %rdx, %rsi\n"
	"movq %rcx, %rdx\n"
	"callq *%rax\n"
	".globl _darling_test_async_return\n"
	"_darling_test_async_return:\n"
	"btrq $60, (%rbp)\n"
	"leave\n"
	"retq\n"
);

#define FRAME_CAPACITY 16
#define GUARD UINT64_C(0xa5a5a5a5a5a5a5a5)

struct guarded_frames {
	uintptr_t before;
	void *frames[FRAME_CAPACITY];
	uintptr_t after;
};

static void reset_frames(struct guarded_frames *frames)
{
	frames->before = GUARD;
	frames->after = GUARD;
	for (size_t index = 0; index < FRAME_CAPACITY; ++index)
		frames->frames[index] = (void *)(uintptr_t)GUARD;
}

static void check_unused(const struct guarded_frames *frames, size_t used)
{
	assert(frames->before == GUARD);
	assert(frames->after == GUARD);
	for (size_t index = used; index < FRAME_CAPACITY; ++index)
		assert(frames->frames[index] == (void *)(uintptr_t)GUARD);
}

int main(void)
{
	backtrace_async_fn capture = (backtrace_async_fn)dlsym(RTLD_DEFAULT,
		"backtrace_async");
	decode_frame_fn decode = (decode_frame_fn)dlsym(RTLD_DEFAULT,
		"pthread_stack_frame_decode_np");
	assert(capture != NULL);
	assert(decode != NULL);

	uintptr_t successor[2] __attribute__((aligned(16))) = {0, 0};
	uintptr_t tagged[2] __attribute__((aligned(16))) = {
		(uintptr_t)successor | (UINT64_C(1) << 60), (uintptr_t)&puts
	};
	uintptr_t return_address = 0;
	assert(decode((uintptr_t)tagged, &return_address) == (uintptr_t)successor);
	assert(return_address == (uintptr_t)&puts);

	const pthread_key_t swift_task_key = 103;
	void *previous_task = pthread_getspecific(swift_task_key);
	assert(pthread_setspecific(swift_task_key, NULL) == 0);
	uint32_t task_id = UINT32_MAX;
	assert(capture(NULL, 0, &task_id) == 0);
	assert(task_id == 0);

	struct guarded_frames frames;
	reset_frames(&frames);
	size_t count = darling_test_async_frame(NULL, frames.frames,
		FRAME_CAPACITY, &task_id, capture);
	assert(count > 0 && count <= FRAME_CAPACITY);
	assert(frames.frames[0] == (const void *)darling_test_async_return);
	assert(task_id == 0);
	check_unused(&frames, count);

	struct async_context contexts[3] = {
		{.parent = (uintptr_t)&contexts[1], .resume = (uintptr_t)&puts},
		{.parent = (uintptr_t)&contexts[2], .resume = (uintptr_t)&malloc},
		{.parent = 0, .resume = (uintptr_t)&pthread_self},
	};
	struct {
		void *reserved[4];
		uint32_t flags;
		uint32_t id;
	} task = {.reserved = {NULL}, .flags = 0, .id = UINT32_C(0x12345678)};
	assert(pthread_setspecific(swift_task_key, &task) == 0);
	void *expected[] = {
		(void *)darling_test_async_return,
		(void *)((uintptr_t)&puts + 1),
		(void *)((uintptr_t)&malloc + 1),
		(void *)((uintptr_t)&pthread_self + 1),
	};
	const size_t capacities[] = {1, 2, 4, FRAME_CAPACITY};
	for (size_t trial = 0; trial < sizeof(capacities) / sizeof(capacities[0]); ++trial) {
		const size_t capacity = capacities[trial];
		const size_t expected_count = capacity < 4 ? capacity : 4;
		reset_frames(&frames);
		task_id = 0;
		count = darling_test_async_frame(contexts, frames.frames,
			capacity, &task_id, capture);
		assert(count == expected_count);
		for (size_t index = 0; index < count; ++index)
			assert(frames.frames[index] == expected[index]);
		if (capacity >= 2)
			assert(task_id == task.id);
		check_unused(&frames, count);
	}

	reset_frames(&frames);
	count = darling_test_async_frame(contexts, frames.frames, 4, NULL, capture);
	assert(count == 4);
	for (size_t index = 0; index < count; ++index)
		assert(frames.frames[index] == expected[index]);
	check_unused(&frames, count);
	assert(pthread_setspecific(swift_task_key, previous_task) == 0);
	puts("DARLING_BACKTRACE_ASYNC_OK");
	return 0;
}
