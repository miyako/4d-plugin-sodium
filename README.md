# sodium

4D plugin exposing **Argon2id** (RFC 9106) password hashing, built on [libsodium](https://libsodium.org).
The commands mirror the built-in bcrypt-based `Generate password hash` and `Verify password hash`, so switching algorithm only means changing the command name.

## Requirements

- 4D 21.1 or later (`compatibilityVersion` 2101 of the test project)
- macOS 10.13+ (universal arm64 + x86_64) or Windows x64

## Installation

Download the latest release from the [Releases](../../releases) page, extract `sodium.bundle` and copy it into the **Plugins** folder of your project (or of the 4D application), then restart 4D.

## Commands

| Command | Returns |
| --- | --- |
| `Argon2 Generate password hash(password : Text {; options : Object}) : Text` | PHC string |
| `Argon2 Verify password hash(password : Text ; hash : Text) : Boolean` | `True` if the password matches |
| `Argon2 Hash needs rehash(hash : Text {; options : Object}) : Boolean` | `True` if the hash should be regenerated |

All commands are thread-safe (preemptive-capable). No lock is held while hashing.

> **Naming deviation:** the requested name `Argon2 Password hash needs rehash` is 33 characters, but 4D command names are limited to 31. The command is therefore called `Argon2 Hash needs rehash`.

### `options` (all optional)

| Property | Unit | Default | Meaning |
| --- | --- | --- | --- |
| `memory` | KiB | `19456` (19 MiB) | memory cost |
| `iterations` | passes | `2` | time cost |

Parallelism is fixed at 1 by libsodium and is not configurable. Unknown properties, non-numeric values, non-integers and values outside libsodium's `crypto_pwhash_MEMLIMIT_*` / `crypto_pwhash_OPSLIMIT_*` range raise an error; nothing is silently clamped.

```4d
$hash:=Argon2 Generate password hash("secret")
$hash:=Argon2 Generate password hash("secret"; New object("memory"; 47104; "iterations"; 1))
// $argon2id$v=19$m=47104,t=1,p=1$<salt>$<hash>

If (Argon2 Verify password hash("secret"; $hash))
    If (Argon2 Hash needs rehash($hash; New object("memory"; 47104; "iterations"; 1)))
        // regenerate and store a new hash
    End if
End if
```

### Behaviour

- The salt is generated internally and cannot be supplied.
- Passwords are converted to UTF-8 explicitly. Passwords longer than **1024 bytes** (UTF-8) raise an error; they are never truncated. Lone UTF-16 surrogates raise an error.
- `Argon2 Verify password hash` returns `False` for a wrong password and for any malformed, bcrypt or non-argon2id hash (no error).
- `Argon2 Hash needs rehash` returns `True` if the stored parameters differ from the requested/default ones, or if the text is not a valid argon2id hash.
- Passwords and hashes are never logged; the UTF-8 password buffer is wiped with `sodium_memzero()`.

### Errors

Errors are raised with the built-in `throw` command (component signature `sodm`) and stored in `Last errors`. On error the commands return an empty text (`Generate`) or `False` (`Verify`, `Hash needs rehash`).
In 4D 21.1 an error raised this way is **not** intercepted by `Try/Catch` or by the `ON ERR CALL` method; without an `ON ERR CALL` method installed 4D aborts the calling method and reports the error. Install any `ON ERR CALL` method and inspect `Last errors` if you need to continue.

| Code | Meaning |
| --- | --- |
| 1 | libsodium could not be initialised |
| 2 | password longer than 1024 bytes |
| 3 | password is not valid Unicode text |
| 4 | options is not an object |
| 5 | unknown option |
| 6 | option is not a number |
| 7 | option out of range / not a positive integer |
| 8 | hashing failed (e.g. out of memory) |

## Parameters

Recommended parameters change over time. Check them against the current [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html) before choosing values. The defaults here (m=19456 KiB, t=2, p=1) match one of the configurations listed there at the time of writing.

## Building from source

Prerequisites: CMake 3.20+, Xcode command line tools (macOS) or Visual Studio 2022 (Windows).

```bash
git clone --recurse-submodules https://github.com/miyako/4d-plugin-sodium.git
cd 4d-plugin-sodium/sodium
```

macOS (universal):

```bash
mkdir -p cmake-build && cd cmake-build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build .
```

Windows:

```pwsh
mkdir cmake-build; cd cmake-build
cmake .. -A x64
cmake --build . --config Release
```

libsodium is linked statically (`SODIUM_STATIC`). The plugin is written to `sodium/sodium-test/Plugins`.

## Tests

With [tool4d](https://developer.4d.com/docs/Admin/cli/) (21.1):

```bash
tool4d --dataless --startup-method=test_all    --project=$(pwd)/sodium-test/Project/sodium.4DProject
tool4d --dataless --startup-method=test_errors --project=$(pwd)/sodium-test/Project/sodium.4DProject
```

Both must print `PASS`. `test_errors` is separate because plugin errors are reported through `Last errors` (see above) and need an `ON ERR CALL` method.

## Third-party software

| Component | Version | Licence |
| --- | --- | --- |
| [libsodium](https://github.com/jedisct1/libsodium) | 1.0.20 (`1.0.20-FINAL`) | ISC |

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This plugin is MIT licensed (see `LICENSE`).

## CI/CD

| Workflow | Trigger | Purpose |
| --- | --- | --- |
| `test.yml` | tag push / manual | build and test on macOS + Windows |
| `bump-version.yml` (Bump version) | manual, branch + patch/minor/major | commit source metadata to the selected branch only; no tag or release |
| `release.yml` (Build) | manual, branch selector only (no inputs) | build, verify, sign, notarize, then tag and create a new GitHub Release (needs the existing Apple signing secrets) |

`VERSION` is the authoritative strict `X.Y.Z` version (no leading zeros or suffixes).
CMake derives both macOS bundle version fields from it; there is no independent
bundle build counter. Bump version checks out the selected commit, refuses an
already-moved branch, and uses a normal non-force branch push to reject concurrent
updates. Bumping never starts Build; run Build separately on the intended branch.

Build pins every checkout to the dispatch's immutable `github.sha`, even if the
branch later moves. It rejects existing `vX.Y.Z` tags **or** releases, including
same-tag drafts without a remote tag, using an authenticated, fully paginated
release listing. Listing/API errors (including 404s), failed later pages, and
malformed responses block publication, as does metadata drift. Publication runs
are serialized across all branches. The Windows x64 artifact is merged into the universal macOS bundle;
the ZIP and DMG are signed/notarized as before, with explicit notarization
acceptance and stapling checks. Only after these succeed does Build recheck
availability, non-force push the tag at the selected SHA, and create a new release
via a create-only API POST with explicit tag, selected SHA, name, and
`draft=false` / `prerelease=false`, retaining the commit-reference release notes.
It then uploads both assets without `--clobber`. It never replaces existing
tags, releases, or assets, and does not use `gh release create`, whose failure
cleanup can delete a partially created release.

If tag push succeeds but release creation/upload fails, the tag (and any partial
release/assets) remains. The workflow reports this partial state; inspect and
recover manually rather than rerunning expecting an overwrite or automatic
rollback. The unrelated `test.yml` tag/manual triggers remain unchanged; a tag
pushed using `GITHUB_TOKEN` does not start another Actions workflow.

Workflow regression checks (Python 3 with PyYAML and `actionlint`, from repo root):

```bash
python3 -m unittest discover -s .github/scripts -p 'test_*.py'
actionlint .github/workflows/release.yml .github/workflows/bump-version.yml
```

## Possible follow-ups (out of scope)

pepper / secret key, associated data, other algorithms (scrypt, bcrypt interop), a `parallelism` option, a `Boolean`-typed return once the plugin API supports it natively, catchable (`Try/Catch`) errors.
