# cf_cmds

Open source reimplementations of the two CoreFoundation commands that ship in
macOS but are absent from Apple's published CoreFoundation source release:
`uuidgen(1)` and `cfprefsd(8)`.

| command | installs to | what it is |
| --- | --- | --- |
| `uuidgen` | `bin/` | a standalone utility, fully reimplemented |
| `cfprefsd` | `sbin/` | the shim that `launchd` runs; the daemon it calls into lives in CoreFoundation |

## uuidgen

Generates a version 4 identifier, or a version 1 one when
`CFUUIDVersionNumber=1` is in the environment, and prints it in canonical
upper-case form. `-hdr` additionally emits a paste-ready
`CFUUIDGetConstantUUIDWithBytes` macro.

The interesting part is how much *smaller* the real argument grammar is than the
strings left in the binary suggest. The shipped `uuidgen` carries a dead
`usage: uuidgen [-hdr] [-n number] [ - | lower | upper | random ]` string and a
dead `#define` variant, neither of which is referenced by any code path. The
only argument the implementation honours is `-hdr`; everything else is a usage
error. Older copies of the `uuidgen(1)` manual page still document `-n`,
`lower`, `upper` and `random` — that documentation is stale.

`CFUUIDVersionNumber` is parsed with `strtoul_l(..., NULL, 0, NULL)`, so base is
inferred: `0x1`, `01` and `1x` all select the time-based generator, because
`strtoul` stops at the first character it cannot use and this code never looks
at `endptr`.

## cfprefsd

`/usr/sbin/cfprefsd` is not the daemon. It is about 200 bytes of code that
relabels itself through libquarantine — so that the sandbox profile keyed to
`com.apple.cfprefsd` is applied in place of one keyed to the image path — and
then tail-calls `__CFXPreferencesDaemon_main`, which CoreFoundation exports.
If the relabel cannot be applied the process logs a fault and exits rather than
running unconfined.

The same executable backs both roles, told apart by the single argument
`launchd` passes and by the Mach service it is expected to export:

| role | label | Mach service |
| --- | --- | --- |
| system-wide daemon, run as root | `com.apple.cfprefsd.xpc.daemon` | `com.apple.cfprefsd.daemon` |
| per-user agent | `com.apple.cfprefsd.xpc.agent` | `com.apple.cfprefsd.agent` |

## Building

```sh
make            # release, artifacts in build/release
make CONFIG=debug
make test       # differential test against /usr/bin/uuidgen
make install    # PREFIX=/usr/local by default
```

The makefile is portable across GNU make and bmake: no pattern rules, no
`ifeq`/`.if` conditionals, no `$(shell)`, with per-config flags in
`make/<CONFIG>.mk`.

There is also a `cf_cmds.xcodeproj` with one target per tool, for building and
debugging from Xcode. It is generated to match the sibling projects rather than
hand-maintained: same `FEEDFACE…` object identifiers, same `Debug`/`Release`
configurations, same `-std=c11 -D_DARWIN_C_SOURCE` flags as the makefile, and
the same `build/debug`, `build/release` and `build/obj/…` output layout, so the
two build systems can be used interchangeably.

Both tools link only CoreFoundation and libSystem, which is what the shipped
binaries' load commands show. In particular `cfprefsd` does *not* link
Foundation or CoreServices.

## Testing

`tools/parity.sh` is a differential test against the system's `uuidgen`. Since
both programs draw fresh random identifiers, identifiers are masked to a
placeholder before comparison; what is actually under test is the output shape,
the ordering of the bytes within it, the argument grammar, and the exit
status. It exits 77 if there is no system `uuidgen` to compare against.

`cfprefsd` is not covered by this harness. It is a daemon, it must run as root,
and starting a second one would displace the system's copy of the same Mach
service. Its parity evidence is instruction-by-instruction comparison against
the shipped binary rather than a runtime diff.

## Status

`uuidgen` is complete and passes differential testing.

`cfprefsd` reproduces the shipped shim exactly, including both quarantine
failure paths and their log messages. The daemon it tail-calls into —
`__CFXPreferencesDaemon_main`, the `CFPrefs*` and `CFPD*` classes, and the XPC
protocol they speak — is CoreFoundation code, not `cfprefsd` code, and is not
part of the published CF source drop. It is being written into `src/CF` so
that the shim's import table stays identical to Apple's; the shim will link
against the rebuilt CoreFoundation rather than resolve the entry point locally.

## Licence

BSD 3-Clause. See `LICENSE`.
