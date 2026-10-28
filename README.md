# Darling

![Darling logo](https://darlinghq.org/img/darling250.png)

Darling is a runtime environment for macOS applications.

Please note that most GUI applications will not run at the moment.

## Download

Packages for some distributions are available for download
under [releases](https://github.com/darlinghq/darling/releases).

## Build Instructions

For build instructions, visit [Darling Docs](https://docs.darlinghq.org/build-instructions.html).

With the documented host dependencies, initialized source submodules and the
Swift Git LFS payload available, a normal production build and installation is:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/darling"
cmake --build build
cmake --install build
DPREFIX="$HOME/.local/state/darling/default" \
  "$HOME/.local/darling/bin/darling" shell echo Hello
```

The default shipping component selection is `COMPONENTS=stock`. Build the default
target and install the whole configured tree; a single `--component core` install
is not a complete shell or developer runtime. Member install rules provide the
guest commands, libraries, framework links, services and runtime data (including
ICU and Objective-C trampolines). No workspace, patch profile, deployment JSON,
test metadata or test-only build option is required.

The runtime root is the configured `CMAKE_INSTALL_PREFIX`, not `DPREFIX`.
Runtime paths are compiled in: configure with the final absolute installation
root rather than relocating with `cmake --install --prefix`. `DESTDIR` remains
available for packaging at that configured location. Installation never stops
running prefixes; shut down each affected prefix explicitly before upgrading
its shared runtime.

### Prefixes

Darling has support for DPREFIXes, which are very similar to WINEPREFIXes. They are virtual “chroot” environments with an macOS-like filesystem structure, where you can install software safely. The default DPREFIX location is `~/.darling`, but this can be changed by exporting an identically named environment variable. A prefix is automatically created and initialized on first use.

The default runtime is rootless and uses a writable per-prefix upper layer over
the installed read-only system tree through vchroot path translation
(`DARLING_EUNION=ON`). It needs no setuid launcher, overlay mount or copied
system-library tree in the prefix. The prefix filesystem must support the
extended attributes used for copy-up, whiteouts and opaque directories.
Explicit privileged diagnostic modes remain available, but are not the default
and require the corresponding host privileges.

One installed runtime can serve separate prefixes:

```sh
DPREFIX="$HOME/.local/state/darling/one" "$HOME/.local/darling/bin/darling" shell echo one
DPREFIX="$HOME/.local/state/darling/two" "$HOME/.local/darling/bin/darling" shell echo two
DPREFIX="$HOME/.local/state/darling/one" "$HOME/.local/darling/bin/darling" shutdown
DPREFIX="$HOME/.local/state/darling/two" "$HOME/.local/darling/bin/darling" shutdown
```

Each prefix owns its first-use skeleton and writable runtime state. Do not copy
the runtime's `bin` or system libraries into it.
Normal installation sets the four `Library`/`System/Library` launch-service
directories to `0755`, independently of the host umask. Keep service directories
and property lists non-group/world-writable: `launchctl` rejects unsafe paths.

`tests/src/installed_runtime.c` is a focused guest behavioral oracle for the
normal `stock` installation. Compile it with the guest CLT and run it in a fresh
prefix; it checks shipping resources and compares paginated bulk directory
enumeration against `readdir`, including rewind. It does not inject system files.

The normal `DARLING_EUNION` build resolves the immutable installation tree and
writable guest prefix in production; it does not require a FUSE overlay.
Typed mutation transactions authorize copy-up, namespace changes and metadata
updates before their native effects. Managed writes never target the lower
image, while a read-only lower open performs no eager copy. Internal whiteout
and opacity attributes are not guest xattrs. Explicit `/Volumes/SystemRoot`
escapes remain unmanaged host operations, not sandboxed image access.
Traversing a layer's host pathname through that escape does not enable union
fallback: an absent prefix entry remains absent even when the installed lower
tree supplies the corresponding guest path.

Descriptor promotion retains opened-object authority after unlink or replacement
and preserves explicit dup lineages, flags and offsets. Directory aliases share
one merged cursor and rewind state, including through promotion. Namespace
admission and protected native-FD ownership span processes and runtime images;
the virtual alias/cursor registries themselves remain image-local. Copy-up does
not provide synthetic stable inode numbers or universal inherited-cursor
coherence across processes/images.
New protected descriptor reservations obey the current soft `RLIMIT_NOFILE`,
including after a forked service lowers its limit before exec. Existing high
descriptors retain their ownership until close; lowering or restoring the limit
does not rebase their identities or change the service's configured limits.

Native server dispatch preserves both a suspended native stack and an unfinished
XNU wait continuation. Ordinary calls wait until the active call finishes;
they cannot discard its stack, held locks or pending cleanup. Only kernel wait
finalization changes the wait result and clears the wait state; physical
microthread dispatch does not.
Management-plane admission uses the same busy-state rule. A nonqueueable request
is disarmed while the prior wait remains active, not reported as completed or
executed later without its owner. A queueable request waits for that call to finish.
Process-scoped console opens select a runnable thread of the same task; they remain
nonqueueable at admission rather than retaining a refused request behind a busy Call.
Admission is not completion: an admitted Call may suspend in native code.
Its reply readiness and transfer from service-pass to deferred ownership are
serialized with the Thread state. Only a recorded final reply permits inline
completion. Deferred completion owns both body translation and descriptor-token
publication; the service pass must not also consume or forward that reply.
Checkout retains its Thread completion owner until a suppressed management reply
is recorded, even across exec teardown. Ordinary ring/datagram replies keep their
detached destination.
Interrupt entry preserves an already committed semaphore grant, and neither
interrupt completion nor a previous grant may satisfy a later wait.
Executable-path and supplementary-group RPCs use the ordinary ring request/reply
path. Their arrays travel through process-memory copies, not variable-sized RPC
payloads; neither operation requires a per-thread socket or caller-side upcall.
The public `getgroups` capacity is signed: negative capacities return `EINVAL`
before any group-buffer access, including when the process has no groups.
The opt-in `DSERVER_TEST_TRACE_FILE` records semaphore begin/reply events for
both ordinary RPC and direct management signals. A management signal's terminal
record follows answer publication; tracing does not dispatch a Call on the
thread whose wait the signal may release.

The directory-pin regression inventories live native directory objects, not
every number returned by procfs: a runtime transient may close before its
buffered entry is consumed. It excludes the inventory observer's own directory
identity, and requires exact native pin identities after observer release and
around guest close/dup2 attempts, before enumeration can acquire new authority.
A guarded `fcntl` returning `EBADF` alone proves no native liveness. The final
directory close must remove each native pin before real descriptor-number reuse.

`tests/src/eunion_{state,mutation_state,xattr_state,fd_lifetime}_guest.c` provide
guest regressions for these transitions using the optional normally installed
`tests-data`. Compile and run them with guest CLT against the complete matching
kernel, dyld and mldr installation, not injected replacement runtime libraries.

Kernel `__simple_kprintf` diagnostics use the existing process-management plane,
including before a calling thread has attached its RPC ring. The ordinary server
`Kprintf` call still owns guest-memory copying, trailing-whitespace removal and
logging policy. The default log cutoff remains `Error`; to inspect these `Info`
records, set the existing host-side `DSERVER_LOG_LEVEL=info` before starting an
idle prefix's server. Changing a guest environment after server startup does not
reconfigure that server. `tests/src/kprintf_logging.c`, compiled with guest CLT,
checks exact newly appended payloads in `/var/log/dserver.log` and whitespace
removal. It requires that explicit diagnostic logging setting and does not enable
diagnostics in ordinary production launches.

Management protocol version 3 reserves a request through matching reply
consumption. Constructing, queued and unread completed requests are not free
slots, and a published call is not republished on a timer. An abandoned
reservation fails closed for that process incarnation; restart the process
rather than reclaiming a slot whose owner may still be alive.
The futex notification word is an independent event epoch, not a request
sequence. Transport readiness, claim and completion each increment it before
waking waiters. A notification between the consumer's final reply check and its
kernel wait must invalidate the sampled epoch, even for the first request or
multiple notifications for the same sequence.
Retained completion mappings are not active courier recipients. Descriptor
delivery requires a live generation and a connection authenticated to that exact
process; another process's loader connection is never a fallback route.

Unix socket translation keeps Darwin's 104-byte pathname capacity separate from
Linux's 108-byte host capacity. Expanded host pathnames must include a terminator;
107-byte names fit and longer names return `ENAMETOOLONG`, never truncation.
Returned addresses honor the caller's capacity. The guest fixture
`tests/src/unix_sockaddr_boundary.c` exercises those host-expansion boundaries,
stream/datagram roundtrips and short output buffers.

Shell startup failures emit a failure-only `shellspawn failed at STAGE` diagnostic
on the service or redirected command stderr, in addition to the launcher's error
result. The stage distinguishes command/descriptor framing, exec-status pipe,
shell exec and child wait failures. Incomplete command frames and invalid
descriptor control messages report `EPROTO`, not an unrelated earlier errno.
The host launcher formats those Darwin errno values with the vendored libc
table, not Linux's differently numbered `strerror()` table.

The xtrace exec hook preserves its settings as exact NUL-terminated environment
entries, including empty values. `tests/src/xtrace_exec_environment_guest.c`
exercises a traced exec with those settings removed from the caller's environment
and verifies their reconstructed values in the new guest image.

Code-signing requirement compilation uses the vendored ANTLR lexer and parser
already linked into Security, rather than loading a separate `csparser.bundle`.
`tests/src/security_requirement_guest.c` covers valid anchor/identifier
requirements, incomplete-syntax rejection, and recovery after a syntax error
without changing subsequent requirement bytecode.

On Linux, Security's single-pass content hashing uses a private
`POSIX_FADV_NOREUSE` replacement-policy hint instead of Darwin's cache-bypass
`F_NOCACHE`; this does not implement `F_NOCACHE` for other callers.
`tests/src/security_static_validation_guest.c` validates a genuine ad-hoc
SHA-256 signature and requires modified content to fail the same public API.

Mach-port channel admission may execute on another runnable thread of the same
task when main is parked. Opening retains the task's IPC space; the later read
uses its explicit receive requester. `tests/src/plane_mach_port_open_guest.c`
requires channel message delivery and subsequent port construction before the
worker releases main, including after closing and reopening the channel.

Single-path creation resolves visibility under exclusive namespace admission.
Concurrent creators therefore observe the committed object rather than a stale
pre-admission snapshot. `tests/eunion_mkdir_race_guest.c` requires exactly one
successful `mkdir` per round and `EEXIST` from every competing creator.

On x86-64 hosts, the optional native `darling_bsd_syscall_abi_test` target
exercises the production BSD syscall dispatcher with both guest entry-stack
alignments, eight arguments and unknown-syscall handling. Build it explicitly
with `cmake --build build --target darling_bsd_syscall_abi_test`, then run
`build/src/startup/mldr/darling_bsd_syscall_abi_test`. This host regression is
not a substitute for guest boot or installed-runtime acceptance.

### Hello world

Let's start with a Hello world:

````
$ darling shell echo Hello world
Hello world
````

Congratulations, you have printed Hello world through Darling's OS X system call emulation and runtime libraries.

### Installing software

You can install `.pkg` packages with the installer tool available inside shell. It is a somewhat limited cousin of OS X's installer:

````
$ darling shell
Darling [~]$ installer -pkg mc-4.8.7-0.pkg -target /
````

The Midnight Commander package from the above example is [available for download](https://darling-misc.s3.eu-central-1.amazonaws.com/mc-4.8.7-0.pkg).

You can uninstall and list packages with the `uninstaller` command.

### Working with DMG images

DMG images can be attached and detached from inside `darling shell` with `hdiutil`. This is how you can install Xcode along with its toolchain and SDKs (note that Xcode itself doesn't run yet):

````
Darling [~]$ hdiutil attach Xcode_7.2.dmg
/Volumes/Xcode_7.2
Darling [~]$ cp -r /Volumes/Xcode_7.2/Xcode.app /Applications
Darling [~]$ export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk
Darling [~]$ echo 'void main() { puts("Hello world"); }' > helloworld.c
Darling [~]$ /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang helloworld.c -o helloworld
Darling [~]$ ./helloworld
Hello world
````

Congratulations, you have just compiled and run your own Hello world application with Apple's toolchain.

### Working with XIP archives

Xcode is now distributed in `.xip` files. These can be installed using `unxip`:

```
cd /Applications
unxip Xcode_11.3.xip
```
