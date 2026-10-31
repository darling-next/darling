#define _GNU_SOURCE
#include "anonymous_vm.h"
#include "signal_atomic.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Linux owns current protection and residency; this ledger owns only the Mach
 * attributes Linux does not expose. Never allocate through libc while locked:
 * metadata slabs use raw mmap and are process-private across fork. */
struct interval {
	uintptr_t start, end;
	int maximum, inheritance;
	struct interval* next;
};
static pthread_mutex_t vm_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct interval *intervals, *free_nodes;

/* Mach vm_map/protect/inherit rules follow vendored XNU vm_user.c/vm_map.c;
 * Linux supplies mmap(NOREPLACE), mprotect and MADV_DONTFORK/DOFORK. Vendored
 * duct-tape, current darlinghq/darlingserver and VibeDarling still provide only
 * a success stub for mach_vm_map_external. XNU's pmap/VM-object implementation
 * cannot back a native Linux task directly, so the existing S2C mmap backend
 * is reused here, with missing Mach attributes maintained process-wide. */

static long native_mmap(uintptr_t address, uintptr_t size, int protection, int flags, int fd, int64_t offset) {
#ifdef SYS_mmap2
	if (offset & 4095) { errno = EINVAL; return -1; }
	if ((uint64_t)offset / 4096 > ULONG_MAX) { errno = EOVERFLOW; return -1; }
	return syscall(SYS_mmap2, address, size, protection, flags, fd, (unsigned long)((uint64_t)offset / 4096));
#else
	return syscall(SYS_mmap, address, size, protection, flags, fd, offset);
#endif
}

static struct interval* node_get(void) {
	if (!free_nodes) {
		long p = native_mmap(0, 4096, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == -1) return NULL;
		struct interval* nodes = (void*)p;
		for (size_t i = 0; i < 4096 / sizeof(*nodes); ++i) {
			nodes[i].next = free_nodes;
			free_nodes = &nodes[i];
		}
	}
	struct interval* n = free_nodes;
	free_nodes = n->next;
	return n;
}
static void node_put(struct interval* n) {
	n->next = free_nodes;
	free_nodes = n;
}
static void split_at(uintptr_t address, struct interval* spare) {
	for (struct interval* n = intervals; n; n = n->next) {
		if (n->start < address && address < n->end) {
			*spare = *n;
			spare->start = address;
			n->end = address;
			n->next = spare;
			return;
		}
	}
	if (spare) node_put(spare);
}

static int needs_split(uintptr_t address) {
	for (struct interval* n = intervals; n; n = n->next)
		if (n->start < address && address < n->end) return 1;
	return 0;
}
static int prepare_splits(uintptr_t start, uintptr_t end, struct interval** left, struct interval** right) {
	int a = needs_split(start), b = needs_split(end);
	*left = a ? node_get() : NULL;
	*right = b ? node_get() : NULL;
	if ((a && !*left) || (b && !*right)) {
		if (*left) node_put(*left);
		if (*right) node_put(*right);
		return -ENOMEM;
	}
	return 0;
}
static void remove_range(uintptr_t start, uintptr_t end) {
	struct interval** link = &intervals;
	while (*link) {
		struct interval* n = *link;
		if (n->start >= end) break;
		if (n->end > start) {
			*link = n->next;
			node_put(n);
		} else link = &n->next;
	}
}
static void insert(struct interval* n) {
	struct interval** link = &intervals;
	while (*link && (*link)->start < n->start) link = &(*link)->next;
	n->next = *link;
	*link = n;
}
static void coalesce(void) {
	for (struct interval* n = intervals; n && n->next;) {
		struct interval* next = n->next;
		if (n->end == next->start && n->maximum == next->maximum &&
			n->inheritance == next->inheritance) {
			n->end = next->end;
			n->next = next->next;
			node_put(next);
		} else n = next;
	}
}

static int range(const struct darling_vm_request* r, uintptr_t* start, uintptr_t* end) {
	if (r->address > UINTPTR_MAX || r->size > UINTPTR_MAX ||
		r->size > UINTPTR_MAX - 4095) return -EINVAL;
	if ((r->address & 4095) && r->operation != DARLING_VM_MMAP) return -EINVAL;
	uintptr_t length = (r->size + 4095) & ~(uintptr_t)4095;
	*start = r->address & ~(uintptr_t)4095;
	if (length > UINTPTR_MAX - *start) return -EINVAL;
	*end = *start + length;
	return 0;
}

/* Read only the fixed numeric prefix of /proc/self/maps. No stdio, malloc,
 * guest syscalls or RPC under the VM lock. Return the containing/next mapping. */
static int linux_region(uintptr_t address, uintptr_t* start, uintptr_t* end, int* protection,
	uint64_t* map_offset, int* shared) {
	int fd = syscall(SYS_openat, AT_FDCWD, "/proc/self/maps", O_RDONLY | O_CLOEXEC, 0);
	if (fd < 0) return -errno;
	char buffer[4096], prefix[64];
	size_t used = 0;
	int result = -ENOENT;
	for (;;) {
		long count = syscall(SYS_read, fd, buffer, sizeof(buffer));
		if (count < 0) { if (errno == EINTR) continue; result = -errno; break; }
		if (!count) break;
		for (long i = 0; i < count; ++i) {
			char c = buffer[i];
			if (c != '\n') { if (used + 1 < sizeof(prefix)) prefix[used++] = c; continue; }
			prefix[used] = 0;
			uintptr_t bounds[2] = {0, 0};
			size_t pos = 0;
			int valid = 1;
			for (int b = 0; b < 2; ++b) {
				int digits = 0;
				while (pos < used) {
					unsigned digit;
					char h = prefix[pos];
					if (h >= '0' && h <= '9') digit = h - '0';
					else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
					else break;
					if (bounds[b] > (UINTPTR_MAX - digit) / 16) valid = 0;
					bounds[b] = bounds[b] * 16 + digit;
					++pos; ++digits;
				}
				if (!digits || pos >= used || prefix[pos++] != (b ? ' ' : '-')) valid = 0;
			}
			if (valid && bounds[1] > address && pos + 5 <= used) {
				*start = bounds[0]; *end = bounds[1];
				*protection = (prefix[pos] == 'r' ? PROT_READ : 0) |
					(prefix[pos + 1] == 'w' ? PROT_WRITE : 0) |
					(prefix[pos + 2] == 'x' ? PROT_EXEC : 0);
				if (shared) *shared = prefix[pos + 3] == 's';
				pos += 5;
				uint64_t offset = 0;
				while (pos < used) {
					unsigned digit;
					char h = prefix[pos++];
					if (h >= '0' && h <= '9') digit = h - '0';
					else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
					else break;
					offset = offset * 16 + digit;
				}
				if (map_offset) *map_offset = offset;
				result = 0;
				goto done;
			}
			used = 0;
		}
	}
 done:
	syscall(SYS_close, fd);
	return result;
}
static int covered(uintptr_t start, uintptr_t end) {
	while (start < end) {
		uintptr_t a, b; int p;
		int status = linux_region(start, &a, &b, &p, NULL, NULL);
		if (status || a > start) return status && status != -ENOENT ? status : -ENOMEM;
		start = b;
	}
	return 0;
}
static int region(uintptr_t address, struct darling_vm_result* out) {
	uintptr_t start, end; int protection;
	int status = linux_region(address, &start, &end, &protection, &out->map_offset, &out->shared);
	if (status) return status;
	uintptr_t linux_start = start;
	uintptr_t point = address > start ? address : start;
	out->maximum = 7;
	out->inheritance = 1; /* VM_INHERIT_COPY */
	for (struct interval* n = intervals; n; n = n->next) {
		if (n->end <= point) { if (n->end > start) start = n->end; continue; }
		if (n->start > point) { if (n->start < end) end = n->start; break; }
		if (n->start > start) start = n->start;
		if (n->end < end) end = n->end;
		out->maximum = n->maximum;
		out->inheritance = n->inheritance;
		break;
	}
	out->map_offset += start - linux_start;
	out->address = start;
	out->size = end - start;
	out->protection = protection;
	return 0;
}

/* Materialize implicit default intervals before any protection/inheritance OS
 * mutation. Failure may leave harmless default nodes, but never changed rights. */
static int fill_range(uintptr_t start, uintptr_t end) {
	uintptr_t at = start;
	while (at < end) {
		struct interval* n = intervals;
		while (n && n->end <= at) n = n->next;
		if (n && n->start <= at) { at = n->end; continue; }
		struct interval* fresh = node_get();
		if (!fresh) return -ENOMEM;
		fresh->start = at;
		fresh->end = n && n->start < end ? n->start : end;
		fresh->maximum = 7; fresh->inheritance = 1;
		at = fresh->end;
		insert(fresh);
	}
	return 0;
}

/* Vendored XNU vm_map_protect/remap_extract makes a COW mapping, preserves
 * inheritance, and adds only WRITE to maximum. Current upstream retains that
 * contract (with additional platform security checks). Its VM objects/pmap
 * cannot back a native Linux task. Existing MAP_PRIVATE mappings already
 * provide write isolation, so neither a remap nor a page copy is necessary.
 *
 * Linux has no transactional shared-to-private conversion. Do not replace a
 * live shared mapping with a copied anonymous buffer: later remap/protection
 * failure cannot reliably restore the original mapping. Reject it unchanged.
 * Likewise mprotect across native VMAs can partially succeed; reject a
 * multi-VMA rights change rather than offer non-atomic COPY semantics. */
static int copy_protection(uintptr_t start, uintptr_t end, int protection,
	int set_maximum, int* native_protection) {
	uintptr_t at = start;
	int regions = 0, current = 0, needs_change = 0;
	while (at < end) {
		uintptr_t a, b;
		int p, shared;
		int status = linux_region(at, &a, &b, &p, NULL, &shared);
		if (status || a > at) return status && status != -ENOENT ? status : -ENOMEM;
		if (shared) return -ENOTSUP;
		int desired = set_maximum ? p & protection : protection;
		needs_change |= desired != p;
		current = p;
		/* Only distinguish a single native VMA from multiple VMAs. */
		regions = regions ? 2 : 1;
		at = b;
	}
	*native_protection = -1;
	if (!needs_change) return 0;
	if (regions != 1) return -ENOTSUP;
	*native_protection = set_maximum ? current & protection : protection;
	return 0;
}

static int mach_copy_protect(uintptr_t start, uintptr_t end, int protection, int set_maximum) {
	int native_protection;
	int status = copy_protection(start, end, protection, set_maximum, &native_protection);
	if (status) return status;
	/* COPY grants WRITE in the new private object, never READ or EXECUTE
	 * that the original maximum did not authorize. Validate the whole range
	 * before changing mappings or attributes. */
	int change_maximum = set_maximum;
	for (struct interval* n = intervals; n && n->start < end; n = n->next) {
		if (n->end <= start) continue;
		if (protection & ~(n->maximum | PROT_WRITE)) return -EACCES;
		if (!(n->maximum & PROT_WRITE)) change_maximum = 1;
	}
	if (change_maximum) {
		struct interval *left, *right;
		status = prepare_splits(start, end, &left, &right);
		if (status) return status;
		split_at(start, left); split_at(end, right);
	}
	if (set_maximum) {
		status = fill_range(start, end);
		if (status) return status;
	}
	if (native_protection >= 0 &&
		syscall(SYS_mprotect, start, end - start, native_protection) < 0) status = -errno;
	if (!status && change_maximum)
		for (struct interval* n = intervals; n && n->start < end; n = n->next)
			if (n->end > start)
				n->maximum = set_maximum ? protection : n->maximum | PROT_WRITE;
	if (change_maximum && !set_maximum) coalesce();
	return status;
}

static int operation_locked(const struct darling_vm_request* r, struct darling_vm_result* out) {
	if (r->reserved) return -EINVAL;
	if (r->operation == DARLING_VM_REGION) {
		if (r->address > UINTPTR_MAX) return -EINVAL;
		return region(r->address, out);
	}
	uintptr_t start, end;
	int status = range(r, &start, &end);
	if (status) return status;
	if (r->operation == DARLING_VM_MMAP && (r->flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) &&
		(r->address & 4095)) return -EINVAL;
	if (r->operation == DARLING_VM_MMAP || r->operation == DARLING_VM_ANONYMOUS_MAP) {
		if (end == start || (r->protection & ~7)) return -EINVAL;
		int flags = r->flags;
		uintptr_t length = end - start, reservation = length;
		uintptr_t mask = 0;
		int inheritance = 1, maximum = 7;
		if (r->operation == DARLING_VM_ANONYMOUS_MAP) {
			/* Darwin VM_FLAGS_ANYWHERE/OVERWRITE and VM_MAKE_TAG only. */
			if ((r->flags & ~0xff004001u) || (r->maximum & ~7) ||
				(r->protection & ~r->maximum)) return -EINVAL;
			if (r->inheritance != 1 && r->inheritance != 2) return -ENOTSUP;
			if (r->mask > UINTPTR_MAX) return -EINVAL;
			mask = r->mask | 4095;
			maximum = r->maximum; inheritance = r->inheritance;
			flags = MAP_PRIVATE | MAP_ANONYMOUS;
			if (!(r->flags & 1)) {
				if (start & mask) return -EINVAL;
				flags |= r->flags & 0x4000 ? MAP_FIXED : MAP_FIXED_NOREPLACE;
			} else {
				if (mask > UINTPTR_MAX - length) return -EINVAL;
				reservation += mask & ~(uintptr_t)4095;
			}
		}
		struct interval *left = NULL, *right = NULL, *fresh = NULL;
		if ((flags & MAP_FIXED) && prepare_splits(start, end, &left, &right)) return -ENOMEM;
		if (maximum != 7 || inheritance != 1) {
			fresh = node_get();
			if (!fresh) {
				if (left) node_put(left);
				if (right) node_put(right);
				return -ENOMEM;
			}
		}
		long mapped = native_mmap(start, reservation, r->protection, flags, r->fd, r->offset);
		if (mapped == -1) { status = -errno; goto map_failed; }
		uintptr_t base = mapped, address = base;
		if (mask && !(flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))) address = (base + mask) & ~mask;
		/* Old kernels may ignore NOREPLACE. Never accept a different address. */
		if ((flags & MAP_FIXED_NOREPLACE) && address != start) {
			syscall(SYS_munmap, base, reservation); status = -EEXIST; goto map_failed;
		}
		if (inheritance == 2 && syscall(SYS_madvise, base, reservation, MADV_DONTFORK) < 0) {
			status = -errno;
			syscall(SYS_munmap, base, reservation);
			if (flags & MAP_FIXED) {
				split_at(start, left); split_at(end, right); left = right = NULL;
				remove_range(start, end);
			}
			goto map_failed;
		}
		if (address > base && syscall(SYS_munmap, base, address - base) < 0) {
			status = -errno;
			syscall(SYS_munmap, base, reservation);
			goto map_failed;
		}
		if (address + length < base + reservation &&
			syscall(SYS_munmap, address + length, base + reservation - address - length) < 0) {
			status = -errno;
			/* The prefix is already released: never unmap it again, since a
			 * native thread could have allocated in that hole meanwhile. */
			syscall(SYS_munmap, address, base + reservation - address);
			goto map_failed;
		}
		split_at(address, left); split_at(address + length, right);
		remove_range(address, address + length);
		if (fresh) {
			fresh->start = address; fresh->end = address + length;
			fresh->maximum = maximum; fresh->inheritance = inheritance;
			insert(fresh);
		}
		out->address = address; out->size = length;
		out->protection = r->protection; out->maximum = maximum; out->inheritance = inheritance;
		return 0;
 map_failed:
		if (left) node_put(left);
		if (right) node_put(right);
		if (fresh) node_put(fresh);
		return status;
	}
	if (r->operation != DARLING_VM_UNMAP && r->operation != DARLING_VM_PROTECT &&
		r->operation != DARLING_VM_MACH_PROTECT && r->operation != DARLING_VM_INHERIT) return -ENOSYS;
	if (r->operation == DARLING_VM_UNMAP && start == end) return -EINVAL;
	if (start == end) return 0;
	if (r->operation == DARLING_VM_MACH_PROTECT && (r->protection & 0x10)) {
		if (r->protection & ~0x17) return -EINVAL;
		return mach_copy_protect(start, end, r->protection & 7, r->set_maximum);
	}
	if ((r->operation == DARLING_VM_PROTECT || r->operation == DARLING_VM_MACH_PROTECT) &&
		(r->protection & ~7)) return -EINVAL;
	if (r->operation == DARLING_VM_INHERIT && r->inheritance != 1 && r->inheritance != 2) return -ENOTSUP;
	if (r->operation == DARLING_VM_UNMAP || r->operation == DARLING_VM_INHERIT ||
		(r->operation == DARLING_VM_MACH_PROTECT && r->set_maximum)) {
		struct interval *left, *right;
		status = prepare_splits(start, end, &left, &right);
		if (status) return status;
		/* Splits preserve semantics if the following OS call fails. */
		split_at(start, left); split_at(end, right);
	}
	if (r->operation == DARLING_VM_UNMAP) {
		if (syscall(SYS_munmap, start, end - start) < 0) return -errno;
		remove_range(start, end);
		return 0;
	}
	if (r->operation != DARLING_VM_PROTECT) {
		status = covered(start, end);
		if (status) return status;
	}
	if (r->operation != DARLING_VM_INHERIT) {
		for (struct interval* n = intervals; n && n->start < end; n = n->next)
			if (n->end > start && (r->protection & ~n->maximum)) return -EACCES;
	}
	if (r->operation == DARLING_VM_INHERIT ||
		(r->operation == DARLING_VM_MACH_PROTECT && r->set_maximum)) {
		status = fill_range(start, end);
		if (status) return status;
	}
	if (r->operation == DARLING_VM_INHERIT) {
		if (syscall(SYS_madvise, start, end - start,
			r->inheritance == 2 ? MADV_DONTFORK : MADV_DOFORK) < 0) return -errno;
		for (struct interval* n = intervals; n && n->start < end; n = n->next)
			if (n->end > start) n->inheritance = r->inheritance;
		return 0;
	}
	if (r->operation == DARLING_VM_MACH_PROTECT && r->set_maximum) {
		uintptr_t at = start;
		while (at < end) {
			uintptr_t a, b; int p;
			status = linux_region(at, &a, &b, &p, NULL, NULL);
			if (status) return status;
			if (b > end) b = end;
			if (syscall(SYS_mprotect, at, b - at, p & r->protection) < 0) return -errno;
			at = b;
		}
		for (struct interval* n = intervals; n && n->start < end; n = n->next)
			if (n->end > start) n->maximum = r->protection;
	} else if (syscall(SYS_mprotect, start, end - start, r->protection) < 0) return -errno;
	return 0;
}

int mldr_vm_operation(const struct darling_vm_request* request, struct darling_vm_result* result) {
	signal_atomic_mask_t saved;
	mldr_block_async_signals(&saved);
	pthread_mutex_lock(&vm_mutex);
	*result = (struct darling_vm_result){0};
	int status = operation_locked(request, result);
	if (request->operation != DARLING_VM_REGION && request->operation != DARLING_VM_PROTECT &&
		!(request->operation == DARLING_VM_MACH_PROTECT && !request->set_maximum)) coalesce();
	pthread_mutex_unlock(&vm_mutex);
	mldr_restore_signals(&saved);
	return status;
}
void mldr_vm_prefork(void) { pthread_mutex_lock(&vm_mutex); }
void mldr_vm_postfork_parent(void) { pthread_mutex_unlock(&vm_mutex); }
void mldr_vm_postfork_child(void) {
	struct interval** link = &intervals;
	while (*link) {
		struct interval* n = *link;
		if (n->inheritance == 2) { *link = n->next; node_put(n); }
		else link = &n->next;
	}
	vm_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}
