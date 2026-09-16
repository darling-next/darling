#!/usr/bin/env bash
# Regression test for perf #1 (dar-dar6x4-perf-5dq.1): __darling_thread_create() must WAIT
# for the new thread's checkin by BLOCKING (FUTEX_WAIT), not busy-spin sched_yield().
#
# Builds the handshake harness and asserts:
#   - RED:   the old polling-only wait never parks, so the checkin can only be released to a
#            creator that is still polling. The harness must fail AND say so.
#   - GREEN: the adaptive futex wait parks after its bounded spin, so the checkin is released
#            to a blocked creator.
#
# HOST test (plain glibc, no Darling runtime), no wall-clock oracle: see
# thread_create_checkin_wait.c for why the handshake replaced the sampled-CPU check.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc="${CC:-cc}"

"$cc" -O2 -g "$here/thread_create_checkin_wait.c" -o "$work/tccw" -lpthread

# RED proof first: the polling-only wait MUST fail, and must fail with the busy-spin
# diagnosis -- a failure for any other reason would make the RED arm meaningless.
echo "--- RED proof (old sched_yield polling wait, expected to FAIL) ---"
set +e
red_out="$("$work/tccw" spin 2>&1)"
red_rc=$?
set -e
printf '%s\n' "$red_out"
if [ "$red_rc" -eq 0 ]; then
	echo "FAIL: spin path unexpectedly passed -- test does not discriminate busy-spin" >&2
	exit 2
fi
printf '%s\n' "$red_out" | grep -F -q 'FAIL: creator burned' || {
	echo "FAIL: spin path failed without the busy-spin diagnosis (rc=$red_rc)" >&2
	exit 2
}
echo "(RED path failed as expected)"

# GREEN: the production futex path must pass.
echo "--- GREEN (futex wait path, expected to PASS) ---"
"$work/tccw"
