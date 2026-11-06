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

The normal build also installs the source-built `/usr/lib/libmdns.dylib`
client object library. This does not enable the mDNSResponder daemon.
The CLI components also build `RemoteServiceDiscovery.framework` from its
broker/device/service client source. Build, OS-version and model strings are
owned copies of XPC properties; the internal-build accessor returns a boolean.
Service enumeration and heartbeat use the device endpoint. Missing broker
services remain errors or terminal browser callbacks, not synthetic devices.

The CLI image installs a source-built `/usr/lib/libimg4.dylib` for generic-x86
Cryptex chip instantiation with the version-6 ABI. Identity comes from the
Image4 properties in `IODeviceTree:/options`; absent individual properties are
omitted, and an unavailable oracle returns `EXDEV`. This does not manufacture
device identity or provide firmware authentication or Cryptex personalization.

The source-built `/usr/lib/libcryptex.dylib` provides reference-counted attribute
objects, lazy `Restore/BuildManifest.plist` parsing, chip/variant selection and
descriptor-backed manifest attachment/copying. Bundle construction consumes the
directory descriptor; manifest attachment and copying preserve the caller's
descriptors. Copies include data and metadata. These operations do not authenticate
the supplied files. Cryptex personalization and mount/unmount are not implemented
or exported; this library does not yet satisfy Xcode's complete Cryptex imports.

Reuse review found no Cryptex provider in the vendored or reviewed sibling tree.
The reviewed public Apple sources provide Image4 declarations/kernel registration;
device-side TSS clients do not implement these host object/bundle operations.
This implementation reuses Darling's OS objects, Foundation plist parser and
`copyfile`, rather than installing native cache binaries.

Bundled trust-anchor lookup hashes follow Security's X.501 normalization:
only PrintableString values fold ASCII case and horizontal whitespace; other
string encodings remain byte-exact. This does not add anchors or relax chain
validation. Generator regressions run with
`python3 -B -m unittest discover -s src/external/openssl_certificates/tests -v`.

CFNetwork request proxy settings are immutable snapshots. Explicit settings
override system policy across request copies, parcels and ordinary redirects;
an empty dictionary selects direct transport, while NULL restores inheritance.
The connection applies that policy to each initial or replacement HTTP stream.

An unknown XPC Mach-service name invalidates the connection and outstanding
asynchronous replies with `XPC_ERROR_CONNECTION_INVALID`, including requests
queued before activation. A deferred reply retains internal connection state
until its callback completes; releasing the caller's reference does not free
that state early. Other bootstrap failures are not reclassified as absent
services, and established named-peer interruption remains distinct.

`/System/Library/PrivateFrameworks/MobileDevice.framework` is an ordinary
framework-root symlink to
`/Library/Apple/System/Library/PrivateFrameworks/MobileDevice.framework`,
matching the separately installed Apple payload.
The normal image does not ship Darling's old MobileDevice runtime stub or
provide that package. Without the package the link remains dangling; install
the genuine package through the guest installer, without library-path overrides.

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
Managed mutation guards preserve no-follow checks on union-owned leaves.
Unmanaged host paths, including `/dev/fd` process-substitution magic links,
retain native open semantics; an explicit guest `O_NOFOLLOW` remains enforced.

Guest `_exit` uses immediate host termination, without running inherited ELF
`atexit` handlers or flushing host stdio. Normal guest `exit(3)` still performs
guest finalization before reaching the same syscall boundary. This distinction
also applies after fork, when a parent's native worker threads no longer exist.
`tests/src/guest_immediate_exit_guest.c` observes real host and guest handlers,
checks exact child statuses and verifies that the host observer is live.

Anonymous Mach mappings retain their maximum protections and inheritance in
the native address-space owner. Current protections cannot exceed that maximum;
lowering it also clamps current access. Alignment, fixed-address collisions,
partial-range splits and `VM_INHERIT_NONE` across fork use the same authority
through local and server-mediated calls. Unsupported shared/object-backed
mapping modes return errors rather than successful unmapped addresses.
`src/startup/tests/anonymous_vm_guest_test.c` exercises aligned allocation,
maximum-rights enforcement, inheritance and the complete guest map lifecycle.

Mach `VM_PROT_COPY` reuses private native COW mappings without copying pages or
remapping backing files. It can add WRITE to the previous maximum, not arbitrary
READ/EXECUTE, and preserves inheritance and file/shared-alias isolation.
Shared-to-private conversion and current-rights changes across multiple native
VMAs return unsupported before mutation. The guest VM fixture checks these
boundaries, maximum-rights changes and private-file isolation.

`libmacho` provides `swap_fileset_entry_command` for clients using modern Mach-O
fileset records. It byte-swaps every scalar field, including the 64-bit address
and file offset, the entry-name offset and reserved word. The SDK's deprecation
annotation does not remove binary clients' need for the entry point.

URLSession WebSocket connection updates flush pending output before servicing
input on the shared session worker. Plain-socket read callbacks recheck actual
descriptor readiness before advertising bytes: an earlier read may already have
consumed a coalesced event's data. Dropping a stale event rearms the callback,
because these streams do not enable automatic read-callback rearming.
The Swift overlay's `URLSessionNetworking.swift` and `networking_adversarial.swift`
consumers exercise protocol completion, cancellation, slow draining, control
frames and untrusted-TLS rejection through the installed runtime.

PackageKit reads local flat-XAR `PackageInfo` metadata. ID-only component
directives preserve previously supplied paths and versions; explicit later
attributes override them. Bundle enumeration joins nested paths and reports
the nearest packaged ancestor in path order. Malformed XML, unsupported package
format versions and mismatched expected TOC digests fail rather than yielding
partial metadata. TOC/data-integrity parsing is not publisher trust,
authorization or installation; the install-request factory remains unsupported.

The IOKit platform expert publishes `IOPlatformUUID` from the same stable host
identity as `gethostuuid`, without inventing Apple model, serial or board values.
In-band single-service matching returns a registered service or not-found.
Property serialization uses out-of-line data rather than direct writes into a
client's address space.

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
RPC signal-atomic regions restore the incoming mask on every ring and socket
exit, including committed transport failures. They preserve application-blocked
signals rather than leaving the temporary asynchronous-signal block installed.
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

Mach-port refcount instrumentation is disabled by default. Set
`DARLING_DIAG=mach-port` before launching the guest to enable its raw-safe
bootstrap records. `tests/src/mach_port_stdout_guest.c` passes an actual
directory path through redirected stdout around a real Mach-port operation and
requires that the captured path still opens the same directory.

UniformTypeIdentifiers resolves device-model tags from the installed CoreServices
declarations and their ancestor graph. Unknown model codes remain unresolved;
the framework does not infer hardware from the host or invent a data type.
`tests/src/uttype_device_model_guest.m` covers model aliases, identifier equality,
device conformance, misses and unrelated file-type lookup.

Sparse `NSIndexSet` insertion preserves sorted, disjoint ranges. Queries traverse
those ranges without building an unused cache; the existing ivar layout remains
unchanged. `tests/src/indexed_collection_filter_guest.m` checks ordered array
selection, nonmonotonic insertion and set filtering across empty, single-element
and large sparse collections, including more than 32 ranges.

Process activities balance the existing CoreFoundation sudden-termination and
vproc automatic-termination transactions. Ending a token, deallocation and
exceptional block completion release its activity. Sleep-suppression requests
use IOKit assertions and report unavailable power services as errors;
latency-critical scheduling requests are explicitly unsupported.
`tests/src/process_activity_guest.m` checks nested lifetimes, automatic
termination, exceptional completion and rejected requests.

Regular-file `clonefile`/`fclonefileat` uses Linux `FICLONE`, not a byte-copy
substitute. The destination becomes visible only after data and metadata
preparation, without replacing an existing name. Unsupported filesystem/object
capabilities return Darwin `ENOTSUP`; other native errors remain errors.
`tests/src/clonefile_guest.c` checks this capability boundary, failed-publication
cleanup, nofollow, source-offset preservation and destination nonreplacement.

Operation-queue barriers wait for all earlier work to finish, including cancelled
asynchronous operations, and exclude later work until the barrier finishes.
Generation fences preserve that ordering independently of queue priorities.
`tests/src/operation_queue_barrier_guest.m` checks asynchronous completion,
concurrent-executor exclusion, suspension, cancellation and empty queues.

Darling Libinfo's DNS backend owns a private libresolv context for each lookup,
reusing it for sequential AAAA/A queries. Concurrent lookups share neither
transaction state nor resolver sockets. Context destruction preserves query
`errno`; lookup failure remains an error rather than a successful fallback.

Kqueue read events derive EOF from Linux hangup/half-close notifications, not
from the instantaneous `FIONREAD` byte count. An empty live socket or a
zero-length datagram is not EOF; buffered data remains available after a
half-close. `tests/src/kqueue_socket_eof_guest.c` checks zero-length datagrams,
subsequent delivery and buffered stream half-close with reverse-direction I/O.

LibSystem's fork and `posix_spawn` child callbacks use libc's single kqueue
registry, including the mutexes used by descriptor-close callbacks.
`tests/src/kqueue_fork_guest.c` checks that a kqueue is not inherited by the
child and that child cleanup preserves the parent's registered readiness.

XPC pipe replies and received messages transfer a caller-owned reference,
independently of the deserializer's autorelease. Callers release that reference
with `xpc_release`. `tests/src/membership_autorelease_guest.m` exercises the real
OpenDirectory user-name lookup, UID roundtrip and stable UUID across nested
autorelease-pool drains.

Successful deferred XPC sends do not destroy their sent Mach rights again.
The connection retains its send right after queue backpressure; libdispatch
releases the completed message.

The `posix_spawn` child invalidates libsyscall's cached PID before running
libSystem child callbacks. Allocators can therefore distinguish child lock
reinitialization from parent unlock. `tests/src/posix_spawn_pid_cache_guest.c`
checks the PID inside malloc-zone callbacks, non-main-thread spawning, genuine
`ENOENT` propagation, successful child completion and ordinary `fork`.

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

Libc's `_os_crash_msg` preserves the optional format/callback separately from
the composed fatal reason, then terminates the calling process through
`abort_with_payload`. `tests/src/os_crash_message_guest.c` checks the callback,
reason propagation, null inputs and `SIGABRT` while the parent stays alive.

`SecTrustCopyCertificateChain` lazily evaluates trust and returns a caller-owned
snapshot of the evaluated chain, without changing the trust verdict.
`tests/src/security_chain_copy_guest.c` checks snapshot lifetime after trust
release and preserves failure for a fixed expired certificate.

`NSFileManager.homeDirectoryForCurrentUser` returns the directory file URL of
`NSHomeDirectory()`, retaining the runtime's current-user resolution and URL
escaping rather than introducing a separate home-directory cache.

An RPC reply saved during signal delivery retains its original transport.
`InterruptExit` publishes ring replies to the original lane and sequence, even
after the thread attaches a replacement lane; failed ring publication preserves
the UDS delivery path. Native `dserver_interrupt_saved_pending` and
`dserver_interrupt_saved_granted` exercise real semaphore continuations and
interrupt calls, checking the result, correlation, wake and single delivery.

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

On i386, Darwin thread-specific state uses `FS`, separately from native Linux
TLS in `GS`. The current thread's existing GDT descriptor is reused across
loader and emulation images; an image-global descriptor cache is not thread
ownership. Descriptor installation initializes every field and checks the
native result before loading `FS`. A failed native-thread TSD allocation or
installation aborts startup rather than entering guest code with an invalid or
inherited thread identity. The x86_64 TLS path is unchanged.

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
