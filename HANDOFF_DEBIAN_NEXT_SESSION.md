# Handoff: next session on Debian (written from Windows, 2026-08-08 end of day)

> You (a fresh Claude instance, no memory of this session) are about to start
> working on Debian. This document is written so you can orient yourself in
> under two minutes without re-deriving anything below. Read this whole file
> before touching the repo.

---

## 1. One-paragraph summary of what happened today

Today's session (on Windows, see `HANDOFF_WINDOWS_TESTING.md` for the earlier
part of the day) did the structural PKCS#15 refactor that
`HANDOFF_NEXT_PKCS15_PROFILE_REFACTOR.md` described as the next task: rewrote
`pkcs15-starsign.c` to supply a **static** PKCS#15 profile (paths, key
references, PIN structure hardcoded; TokenInfo/label/serial still read live
from the card) instead of dynamically parsing and patching the card's own
AODF/PrKDF, per the OpenSC maintainer's review feedback on PR #3764. It was
validated exhaustively on Windows (physical hardware, real PDF signature
approved by ITI's official validator, real `pje_headless` end-to-end run),
committed, pushed, and the maintainer was replied to on the PR. **That
specific task (`HANDOFF_NEXT_PKCS15_PROFILE_REFACTOR.md`, section 2) is now
DONE** -- see the "STATUS: COMPLETED" note at the top of that file. Don't
redo it.

## 2. Where the actual code lives

The driver source changes are in the **fork of OpenSC**, not in this
docs-only repo:

- Repo: `https://github.com/DiegoRibeirodeSouza/OpenSC`
- Branch: `feature/starsign-cut-s-driver`
- Latest commit: **`7a1c51342`** -- "refactor: supply StarSign's PKCS#15
  layout as static internal data"
- This repo (`starsign-driver-en`) contains a nested `OpenSC/` copy for
  packaging/reference purposes, plus documentation. On Windows, the actual
  working clone used for today's build/test cycle was a **separate**
  checkout at `C:\Users\Diego Ribeiro\projects\OpenSC-pr3764` (not inside
  this repo). On Debian, check whether an equivalent standalone clone
  already exists (see `check-existing-state-before-cloning` -- always check
  what's already on disk before cloning fresh) before assuming you need to
  clone again. Whatever clone you use, make sure it's on
  `feature/starsign-cut-s-driver` and has pulled commit `7a1c51342`.

PR: https://github.com/OpenSC/OpenSC/pull/3764
Today's reply to the maintainer's review thread:
https://github.com/OpenSC/OpenSC/pull/3764#discussion_r3741106485

## 3. What changed in `pkcs15-starsign.c` (read the file, but here's the gist)

- `sc_pkcs15emu_starsign_init()` no longer calls `sc_pkcs15_bind_internal()`
  or installs a `parse_df` hook. It builds the PKCS#15 object tree directly:
  `starsign_read_tokeninfo()`, `starsign_add_pins()`, `starsign_add_certs()`,
  `starsign_add_keys()`.
- `card-starsign.c` (the SELECT-file quirk handling, DRM handshake, MSE SET,
  etc.) is **completely untouched**. Don't re-review it; nothing there
  changed today.
- `pkcs15-syn.c` got one line added: `SC_CARD_TYPE_STARSIGN` in the
  `sc_pkcs15_is_emulation_only()` switch. **This is load-bearing** -- without
  it the new static profile silently never runs, because this card's raw
  PKCS#15 structure is well-formed enough that the generic dynamic parser
  (tried first by default) succeeds on its own, so
  `sc_pkcs15_bind_synthetic()` (where the static profile lives) never gets
  reached. Confirmed this empirically today after quite a bit of confusion --
  see section 6 below if you need to re-verify this on Debian.

### Structural facts about this card model (same across every StarSign CUT S token, confirmed against physical hardware, safe to treat as ground truth -- don't rediscover)

- `EF(TokenInfo)`: path `3f005032`, direct child of the MF. Read live at
  runtime (not hardcoded) so label/serial/manufacturer are correct per
  cardholder.
- `EF(ODF)`: path `3f005031` (not used by the new static code, but useful to
  know if you ever need to fall back to a debug trace).
- Certificate slots, all under `3F00 [virtual 0x3FFF placeholder] 4302
  <final>`: leaf/signature cert = `0114`, intermediate CA 1 = `05a0`,
  intermediate CA 2 = `13ae`, root CA = `1371`.
- PIN: User Pin reference `0x02`, SO Pin reference `0x01`, both path `3f00`
  (MF-level, not inside a DF).
- Current signing key: `key_reference = 1`. Legacy/orphan key (no live cert):
  `key_reference = 0`.

### ID scheme changed -- update any of your own scripts/notes

The old code exposed the signing key/cert as PKCS#11 `CKA_ID` =
`444945474f...` (hex for `"DIEGO RIBEIRO DE SOUZA 2024-10-09..."`, literally
read off the card's own CDF/PrKDF). The new static profile does **not** read
that field from the card anymore (going static means we stopped parsing the
card's CDF/PrKDF at all) -- and hardcoding that exact string as a driver
constant would have leaked my identity into every other StarSign user's
build. So IDs are now small driver-assigned values:

| Object | id |
|---|---|
| Signature Key (current, has live cert) | `01` |
| Signature Certificate (leaf) | `01` |
| Legacy Key (orphan, key_reference 0) | `02` |
| Legacy Public Key | `02` |
| Intermediate CA Certificate 1 | `03` |
| Intermediate CA Certificate 2 | `04` |
| Root CA Certificate | `05` |

If you have Debian-side scripts, aliases, or notes that reference the old
long hex id for `--sign --id ...` or similar, they need updating to `01`.

## 4. What's validated (Windows only so far) vs. what still needs Debian confirmation

**Already proven on Windows today, physical hardware:**
- Clean build (MSVC/nmake).
- `pkcs15-tool -D` dump is identical to the pre-refactor baseline except for
  the intentional id/label differences above (paths, key references, PIN
  reference/flags, usage, access flags all byte-for-byte identical).
- `pkcs11-tool --sign --id 01 ...` returns `CKR_OK`.
- Installed as the "official" Windows driver
  (`C:\Program Files\OpenSC Project\OpenSC\`), a real PDF signed through it
  (pyHanko, `use_raw_mechanism=True`) was approved by ITI's official
  conformance validator (Verificador de Conformidade): `Status de
  assinatura: Aprovado`, full certificate chain valid to the ICP-Brasil
  root.
- End-to-end through `pje_headless` (`PJE_SIGNER_PRIORITY=pkcs11`) -- started
  cleanly and bound its PJeOffice-compatible HTTP server once a port
  conflict with the already-running official PJeOffice Pro (`javaw.exe`
  squatting port 8800) was identified and PJeOffice Pro was closed. That
  port conflict is a Windows-only concern (PJeOffice Pro isn't installed on
  Debian), just flagging it existed in case something superficially similar
  ever shows up.

**NOT yet validated -- this is the actual work for the Debian session:**
1. Pull `feature/starsign-cut-s-driver` (commit `7a1c51342`) into whatever
   OpenSC clone you use on Debian, rebuild (`./bootstrap && ./configure &&
   make`, or whatever this repo's existing build process is -- check
   `install_and_test.sh` in this repo first).
2. **Before testing anything**, take a fresh `pkcs15-tool -D` dump with the
   currently-installed (pre-refactor) Debian build as a baseline, exactly
   like the Windows session did -- this was the single most useful thing
   done today and caught a real bug (a PIN `Auth ID` field I'd forgotten to
   set) that a "looks right" code review would have missed. Save it to a
   file before rebuilding.
3. Rebuild with the new code, re-dump, `diff` against the baseline. Expect
   it to match exactly except for the id/label changes in the table above.
   Any other difference is a real regression -- stop and investigate, don't
   rationalize it away (there was a genuine false-alarm scare earlier in
   this project's history from not doing exactly this, see
   `HANDOFF_NEXT_PKCS15_PROFILE_REFACTOR.md` section 3 for that story).
4. **Important unknown**: verify what Debian's installed `opensc.conf`
   actually has for `try_emulation_first` / whether the emulator was already
   being exercised there before today's `is_emulation_only()` change. On
   Windows, the default config meant the *old* code's `parse_df` patch logic
   might never have actually been running via the default path either
   (`sc_pkcs15_bind_internal()` succeeds on its own and wins by default
   unless `try_emulation_first` is set inside a `framework pkcs15 { }` block
   in `opensc.conf`, or the card type is in `sc_pkcs15_is_emulation_only()`
   -- learned this the hard way today, see section 6). If Debian's
   packaged `opensc.conf` sets `try_emulation_first = yes;` for some reason
   (some distros do this for known cards), the *old* code's emulator path
   may have actually been exercised there all along, in which case this
   whole refactor is more consequential for Debian than it turned out to be
   for today's Windows testing. Worth understanding, not just assuming.
5. Isolated sign test (`pkcs11-tool --sign --id 01 ...`, not `--test`) to
   confirm `CKR_OK` after the rebuild, same as Windows.
6. Only after all of the above matches: recompile and **republish the
   `.deb` packages** in both `starsign-driver` and `starsign-driver-en`
   GitHub releases -- these were already known-stale before today (missing
   yesterday's `f0cd700e9` review-feedback commit) and are now stale by one
   more commit (`7a1c51342`). See `install_and_test.sh` / the packaging
   scripts already in this repo for the existing process.

## 5. Outstanding PR checklist item

PR #3764's checklist still has "macOS token is tested" unchecked -- no
access to macOS hardware, not something to try to solve on Debian. Not
blocking.

## 6. One methodological trap to avoid re-falling into

Early in today's session, a rebuilt driver produced a `pkcs15-tool -D` dump
**byte-identical** to the pre-refactor baseline even though the new code
used completely different labels and ids -- which is impossible if the new
code had actually run. Spent real time confused before realizing: by
default, `sc_pkcs15_bind()` tries the generic dynamic parser
(`sc_pkcs15_bind_internal()`) *first*, and only falls back to the static
emulator (`sc_pkcs15_bind_synthetic()`, where `pkcs15-starsign.c` lives) if
that fails. Since this card's raw PKCS#15 structure parses fine generically,
the emulator was never even being invoked -- the dump matched because it was
still running the *old* code path the whole time. Two ways to force the
emulator path for testing: (a) `try_emulation_first = true;` **nested inside
a `framework pkcs15 { }` block** in `opensc.conf` (NOT directly in `app
default {}` -- that silently does nothing, wasted a debug cycle discovering
this), or (b) what actually shipped: add the card type to
`sc_pkcs15_is_emulation_only()` in `pkcs15-syn.c` so it's forced on by
default for everyone, no config needed. If something similar ever looks
"suspiciously unchanged" after a driver code change on Debian, check which
bind path actually ran (grep a debug trace for `pkcs15-starsign.c` or
`bind_synthetic`) before assuming the change had no effect.

## 7. Links

- PR: https://github.com/OpenSC/OpenSC/pull/3764
- Today's commit:
  https://github.com/DiegoRibeirodeSouza/OpenSC/commit/7a1c5134282c7dff189433ec68f6fe822c772fc8
- Today's PR reply:
  https://github.com/OpenSC/OpenSC/pull/3764#discussion_r3741106485
- Earlier handoffs (context for how we got here):
  `HANDOFF_WINDOWS_TESTING.md`, `HANDOFF_NEXT_PKCS15_PROFILE_REFACTOR.md`
  (now marked completed at the top, historical detail below that note is
  still accurate/useful reference for the card's PKCS#15 data).
