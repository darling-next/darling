#include <stdio.h>
#include <stdlib.h>

/* Linked against the actual production bsd_syscall.S, not a copied model.
 * The host-only define changes Mach-O section/visibility directives only. */
extern long mldr_syscall_abi_probe(long, long, long, long, long, long, long, long);
long (*const mldr_bsd_table[])(long, long, long, long, long, long, long, long)
	__asm__("___bsd_syscall_table") = { mldr_syscall_abi_probe, NULL };
unsigned long mldr_syscall_entry_alignment;
static unsigned calls;

void mldr_syscall_trace(long number, const long* arguments)
	__asm__("___darling_bsd_syscall_trace_c");
void mldr_syscall_trace(long number, const long* arguments)
{
	(void)number;
	(void)arguments;
}

static void require(int condition, const char* detail)
{
	if (!condition) {
		fprintf(stderr, "FAIL BSD syscall ABI: %s\n", detail);
		exit(1);
	}
}

long mldr_syscall_eight(long a, long b, long c, long d, long e, long f, long g, long h)
{
	require(mldr_syscall_entry_alignment == 8, "C handler stack alignment");
	require(a == 11 && b == 22 && c == 33 && d == 44 && e == 55 && f == 66 &&
		g == 77 && h == 88, "all eight syscall arguments preserved");
	++calls;
	return a + b + c + d + e + f + g + h;
}

long mldr_syscall_unknown(long number)
{
	require(mldr_syscall_entry_alignment == 8, "unknown syscall C stack alignment");
	require(number == 1, "unknown syscall number preserved");
	++calls;
	return -38;
}

extern long mldr_syscall_leaf_call(long, long, long, long, long, long, long, long);
extern long mldr_syscall_custom_call(long, long, long, long, long, long, long, long);
extern long mldr_syscall_unknown_leaf(void);
extern long mldr_syscall_unknown_custom(void);

int main(void)
{
	register unsigned long preserved __asm__("r12") = 0x123456789abcdef0UL;
	__asm__ volatile("" : "+r"(preserved));
	require(mldr_syscall_leaf_call(11, 22, 33, 44, 55, 66, 77, 88) == 396,
		"leaf syscall result");
	require(mldr_syscall_custom_call(11, 22, 33, 44, 55, 66, 77, 88) == 396,
		"aligned custom syscall result");
	require(mldr_syscall_unknown_leaf() == -38, "leaf unknown syscall result");
	require(mldr_syscall_unknown_custom() == -38, "custom unknown syscall result");
	__asm__ volatile("" : "+r"(preserved));
	require(preserved == 0x123456789abcdef0UL, "callee-saved register survives dispatch");
	require(calls == 4, "each C handler called exactly once");
	puts("DARLING_BSD_SYSCALL_ABI_OK");
	return 0;
}
