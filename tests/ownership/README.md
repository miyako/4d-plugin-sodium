# Sodium ownership regression

## Provenance and inventory

The audited plugin baseline is `4fdc8eb4a5f0190065bc7b156606d17e84dc3948`.
The canonical guidance was read through the GitHub contents API at
`miyako/skills@e6b492fcb81e5e1a7f7db723594b420e63bf6eb9`,
`4d-skills/skills/4dplugin/SKILL.md`.

| Tracked scope at baseline | Native inventory and build membership |
| --- | --- |
| Plugin: 33 tracked paths, two native files | `sodium/sodium-4dplugin.cpp` and `.h`; CMake compiles the implementation and the complete SDK implementation on both platforms. No additional root native source, wrapper library, generated dispatcher, or internal native directory exists in the tracked tree. |
| SDK `cd97d1876d4258b56d2516e8ad2816f4ee93e0b5`: 893 tracked paths, eight native files | `4D Plugin API/{4DPluginAPI.c,4DPluginAPI.h,EntryPoints.h,Flags.h,PrivateTypes.h,PublicTypes.h}` plus dormant Wizard `Resources/Extras/{4DPlugin.cpp,4DPlugin.h}`. Both actual consumers are syntax-compiled by the runner. |
| SDK dormant project memberships | Wizard Xcode project compiles `4DPluginAPI.c` and `4DPlugin.cpp`; VS template compiles `4DPlugin.cpp` and has resource-copy commands. Neither project participates in the sodium build; neither is executed by this harness. |
| `libsodium-cmake@9b2848dfc1b917a9410f0de9d81059b26cbfaa8d` | Eight tracked paths, no native source of its own. Its CMake file enumerates the libsodium sources, configures `version.h`, and optionally adds tests. It has no custom release/signing build phase. |
| Nested `libsodium@93a7d0d41fe2e32409b5d00386946f491750b7de` | 626 tracked paths, 347 native files (199 C, 148 headers). These implement cryptography, not additional 4D ownership consumers. The recursive tracked inventory was enumerated and checked for 4D calls/includes; no such integration exists there. This is not a cryptography/security audit of those implementations. |

SDK blobs used unchanged:

| Path in `4D Plugin API` | Git blob |
| --- | --- |
| `4DPluginAPI.c` | `8757453c5ebe699e69bdeef1682b2b6bdd220752` |
| `4DPluginAPI.h` | `5b7f6fe94344cfef38011279d68f0b680b7f062b` |
| `EntryPoints.h` | `9e65f10c12a164f4df159bf2eef8711847429e2d` |
| `Flags.h` | `7a5a35539f27430a3fd9cb711d6b789051316471` |
| `PrivateTypes.h` | `a3098cb662cdbc43ba574fdef72a059c4c6d1cd4` |
| `PublicTypes.h` | `9ae20b001bff9128f690fff3e6c3f873b6238333` |
| `4DPluginAPI.def` | `5bbdb82fb724b6f8e307feb01d9f158b802de89d` |

The SDK header's `rev: v14` comment is not an upstream release guarantee.
Windows exports `FourDPackex` through the unchanged `.def`; macOS uses the
SDK entry point's visibility attribute. `FourDPackex` calls the actual
`PluginMain`, whose initialization and three command selectors were inspected.
The manifest has text/object inputs, a text result and two generic variable
results (`:V`), no array parameters. The implementation was traced independently
of that manifest: error helpers, scalar assignments, synchronous callbacks,
owning object/collection getter results, borrowed password/hash parameters and
return helpers are all included.

There are no `pluginClasses`, `CPicture`, pointer-reference constructors,
`createCopy`/`CUTF16String` conversion wrappers, or ownership-bearing wrapper
copy operations in this target, including dormant SDK Wizard sources. Their
picture double-cleanup, creation-reference, byte/unit and implicit-copy
regressions therefore do not apply here. This conclusion comes from the
enumerated source/build memberships and direct consumer reads, not from the
manifest lacking arrays.

## Contracts and fix

The exact SDK implements `PA_CreateVariable(eVK_Unistring)` by creating an empty
string (line 4037), while `PA_SetStringVariable` shallow-overwrites the descriptor
(lines 4261-4265). Initializing an Undefined recipient avoids losing that
independent empty-string owner; it does not remove legitimate cleanup.

Error construction now disposes temporary key strings, clears the string
variable after setting the object property, and clears the error object's
creation-reference argument after the synchronous `throw` command returns.
The callback's successful returned variable is also cleared. Its error is
captured before any cleanup; failed unspecified return storage is not inspected
or cleared. The borrowed options-object argument is deliberately not cleared.

`PA_GetObjectProperty` and `PA_GetCollectionElement` explicitly document that
their successful result must be cleared (SDK lines 637 and 587). Successful
numeric, string, object and collection mismatch results are cleaned up.
`OB Keys`' successful wrong-kind result is cleaned up too. Failed getters are
checked immediately, before kind normalization, conversion or cleanup.
The existing option-error categories/messages are used; no worker exceptions,
new error channel or success-shaped fallback is introduced.

The SDK forwards object property assignment to `EX_SET_OBJ_VALUE` without an
ownership-transfer comment. The synthetic host models persistent object
properties by copying string content/retaining object references, leaving keys
and input values caller-owned. This is an explicit engine-boundary contract,
not an implementation of the closed-source engine. Likewise, the text-variable
conversion boundary returns a borrowed alias for an already-string variable;
the plugin now rejects non-string collection elements before converting them.
Successful owning getters are independent values; matching collection handoff
keeps `keysVar` alive throughout iteration. The harness asserts these balances
and reads callback properties before cleanup, but does not establish a native
engine ABI from those assumptions.

The array control uses the real SDK's `PA_CreateVariable`, `PA_ResizeArray`,
`PA_GetStringInArray` and `PA_SetStringInArray`, not reimplementations. Accepted
assignments dispose the old element and shallow-transfer the new descriptor.
Index zero is accepted. Wrong kind, null data, negative/past-end index and null
descriptor reject without consuming a caller-owned string. A borrowed getter
is copied before replacing its element; resize and final clear release their
owners. No production array code was invented for this scalar-only plugin.

## Isolated reproduction

From the repository root on macOS, with the pinned submodules materialized:

```bash
bash tests/ownership/run.sh "$PWD" /absolute/path/to/ownership-output
```

Only a compiler, shell and standard platform tools are needed. The runner
syntax-compiles the actual plugin against real pinned libsodium headers and the
dormant Wizard consumer against the actual SDK. A missing generated libsodium
version header is produced only in the output directory from the pinned
template and the unchanged CMake version values. No dependency build/install or
plugin CMake/Xcode/VS build is run.

The sanitizer executable includes the actual plugin source, with every helper
and command compiled, and links the **complete unchanged** `4DPluginAPI.c`.
CoreGraphics functions and cryptographic operations have aborting synthetic
boundaries installed before compilation. Only synthetic initialization and
buffer wiping are permitted; there is no hashing, entropy acquisition, network,
keychain, private fixture or real platform operation. Unknown engine selectors,
commands and handle operations abort. An executable import gate rejects
CoreGraphics, crypto, security, dynamic loading, shell, socket and direct file
access imports. Normal compiler/sanitizer/C++ runtime imports remain necessary;
this is not an OS sandbox.

Each named case runs in a separate ASan/UBSan process, with stack-use-after-return
detection and halt-on-UB enabled. Explicit live string, reference and handle
balances must all be zero after caller cleanup. The failed-result fixtures fill
return storage with poisoned bytes and raw `fType=0`; it must never be treated
as a successful string or cleared. Leak fixtures are reclaimed only after
recording a failed measurement so cases remain independent.

The identical final harness was run against a `git archive` of the baseline
and each pinned dependency, then against fixed actual source:

The harness SHA-256 is
`491593bd7b628497d8705605800cb0d30c4784aa2121dc6c344cc5893ef39715`;
the runner SHA-256 is
`4955e356fea90901a212bab582cbb8a1fd0b700cca1a6bc673e0fc20a5451ad3`.
Both executions use those same files, including allocator churn before callback
property reads and after array insertion/replacement.

| Case (each in null-empty and allocated-empty modes) | Baseline | Fixed |
| --- | --- | --- |
| Text ASCII/BMP/supplementary/empty, exact UTF-8 bytes, UTF-16 units, capacities and termination | Pass | Pass |
| Error property/key/object temporaries | Fail: leaked owners | Pass |
| Numeric option control | Pass | Pass |
| Owning string property mismatch | Fail: error owners | Pass |
| Unknown option | Fail: error owners | Pass |
| Owning `OB Keys` wrong-kind result | Fail: result and error owners | Pass |
| Failed command, unspecified result | Fail: error owners | Pass |
| Failed property, unspecified result | Abort: invalid cleanup | Pass |
| Failed collection element, unspecified result | Abort: invalid conversion | Pass |
| Actual `FourDPackex`/`PluginMain` Generate invalid-Unicode path | Fail: error owners | Pass |
| Failed synchronous throw, unspecified result | Fail: error owners | Pass |
| Successful throw owning string/object/collection results (three cases) | Fail: result and error owners | Pass |
| Owning object property mismatch | Fail: error owners | Pass |
| Owning object collection-element mismatch | Abort: invalid conversion | Pass |
| SDK array transfer/rejection/borrow/resize control | Pass | Pass |
| Owning collection property mismatch | Fail: error owners | Pass |
| Password 1024/1025-byte and supplementary/lone-surrogate boundaries | Pass | Pass |
| **Total** | **30 failed, eight passed / 38** | **38 passed / 38** |

Baseline error-temporaries leaves seven strings and one object with null
empties, nine strings and one object with allocated empties. The extra two
allocations identify the two overwritten empty recipients independently of
the other temporary leaks. Fixed cases leave zero owners in both modes.

## Limits and safety

This is synthetic ownership/sanitizer evidence, not a Windows or native 4D
crash reproduction. No real cryptographic path, MSVC build, 4D runtime or native
Boolean return ABI is validated. The existing `return_boolean` helper passes
a local variable through `PA_ReturnVariable`, whose SDK implementation stores
its address; the host's consumption timing is not specified in the bundled
headers. This remains a separate ABI/lifetime question, not a claimed-safe path
or a speculative change to generic returns/manifest metadata.

Allocation/resize failure preservation, `PA_GetCollectionLength`'s internal
error overwrite/uninitialized failed value, engine failure ownership,
pre-v16.3 SDK object construction and engine abort/non-return cleanup are not
certified. The existing helpers still have no dedicated recovery for failures
while constructing the error object itself; no new channel or fallback was
added. Successful allocation tests do not establish OOM safety. The bundled
SDK/ABI headers remain untouched.

All existing workflows, release scripts and native project build/resource
phases were read before validation. Existing workflows are manual or tag-only;
none is invoked. The new `ownership.yml` only runs this unsigned fixture on
relevant paths pushed to `main` or the exact audit branch. It builds no plugin
bundle and signs, notarizes, packages, publishes and uploads nothing.
