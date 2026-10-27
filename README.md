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
