# Handoff: end of the 2026-08-08 Debian session

> You (a fresh Claude instance, no memory of this session) may be picking
> this up later. This covers everything from today that isn't already
> captured in `HANDOFF_DEBIAN_NEXT_SESSION.md` (which only covers the
> driver sync/rebuild/republish task and is already marked completed).

## 1. What's fully done, no action needed

- StarSign CUT S driver synced to PR fork commit `7a1c51342` in both
  `starsign-driver-en` and `starsign-driver`, rebuilt (a3.8 / a3.4),
  installed, and retested on physical hardware. Both repos published as
  `v0.2.1` releases with `.deb` + `.msi` assets.
- Posted a summary comment on PR #3764 about the Debian package update.
- Posted a comment on issue #2580 (the original 2022 "unsupported card"
  report) linking PR #3764, since the issue was closed in 2024 for
  unrelated reasons and GitHub's `Fixes #2580` keyword has no effect on an
  already-closed issue -- there was no automatic cross-reference.
- Removed the proprietary SafeSign driver (`safesignidentityclient`
  package, `libaetpkss.so*`, `~/.safesign`, `/usr/lib/safesign-private`)
  and PJe Office (`~/pjeoffice`, `~/.pjeoffice-pro`, desktop shortcut/icon)
  from the system -- no longer used, replaced entirely by the OpenSC
  driver + `litisdoc` + `pje_headless`. `pjecalc` (a different, unrelated
  tool) was intentionally left untouched.
- Verified, after the SafeSign removal, that the whole stack still works:
  PAM/sudo via smartcard, `litisdoc`'s signing path, and -- most
  convincingly -- **`pje_headless` signed a real authentication challenge
  against the live production PJe SSO server** (`sso.cloud.pje.jus.br`)
  today and got `204` back. This is real-world proof, not just a local
  test.

## 2. `encrypta3`: shelved, two real vaults are permanently locked

Root cause (full technical detail already in `HANDOFF_DEBIAN_NEXT_SESSION.md`'s
status block): raw RSA decrypt of a full 256-byte block through this
token's built-in CCID reader is a hardware dead end -- the reader only
supports short APDUs, and the card rejects ISO 7816-4 chaining outright.
Confirmed identical on both the old and new driver, so it is *not*
something a driver fix can solve. Encrypting is unaffected (public-key-only,
pure software); only decrypting via the token is broken.

Diego decided to set `encrypta3` aside in favor of established Debian
encryption tools (e.g. `gocryptfs`, `veracrypt`, `age`, `gpg`) rather than
chase a hardware-impossible fix.

**Two real vaults on this system are currently unrecoverable via the
token** (confirmed by an actual failed decrypt attempt, not just theory):
- `~/Área de trabalho/GetInteiroTeorDoAcordao.txt.ea3`
- `~/Vídeos/pasta sem título.ea3`

Neither has a recovery password set (`has_pwd=0` in the vault header), and
the token's RSA private key is `CKA_SENSITIVE`/non-extractable, so there is
no software workaround. The only theoretical remaining option, not yet
tried, is testing whether the *proprietary* SafeSign driver's decrypt
implementation handles the operation differently (a completely different
codebase might avoid the chaining issue some other way) -- but SafeSign
was removed from this system today per Diego's request, so trying that
would mean reinstalling it temporarily. Diego was offered this and did not
ask for it to be pursued. If he ever wants those two files back, that is
the next thing to try; otherwise treat them as lost.

(`~/Imagens/wm.ea3` is safe -- it has a recovery password. `~/.agenda3.ea3`
is not a valid `encrypta3` vault by the current header format, likely an
unrelated file -- not investigated further.)

## 3. GnuPG *can* sign with the token, not decrypt (same wall as above)

Verified today with a real `gpgsm --detach-sign` / `--verify` round trip
against the token (via `scdaemon` + `gnupg-pkcs11-scd`, neither of which
were installed before -- both were installed today as regular Debian
packages, not removed, since they're harmless to keep). Signing works for
the same reason `litisdoc` signing works: it only ever needs to move a
~51-byte DigestInfo to the card. `gpgsm --decrypt` was also tested and
hits the identical hardware wall as `encrypta3` (`Erro de cartão`) --
expected, not a new finding, just confirms the limitation is
tool-independent.

The temporary GnuPG config changes made for that test (`scdaemon-program`
override in `gpg-agent.conf`, the ICP-Brasil root CA marked trusted in
`trustlist.txt`) were reverted the same session. GnuPG is back to its
default (no smartcard scdaemon configured) unless Diego sets it up again
on purpose.

## 4. Still open: macOS testing (PR #3764 checklist)

Only remaining unchecked item on the PR checklist. No macOS hardware
available. Discussed and rejected running macOS in a VM on this Debian box
via OSX-KVM/Docker-OSX-style projects -- technically works, but violates
Apple's macOS EULA when run on non-Apple hardware, and Diego already
decided against that approach in an earlier session for the same reason.

Two options were discussed, not yet acted on:
1. **Rent a real cloud Mac** (MacStadium, Scaleway Mac mini, ~US$25-50/mo,
   cancelable) and use a USB-over-network redirector (USB/IP or similar)
   to forward the physical token to it, so the token never leaves Diego's
   hands. Main risk: CCID/smartcard timing doesn't always survive
   USB-over-IP redirection well: this might just not work.
2. **Buy a cheap used Mac mini** (~R$800-1500, one-time) as a fallback if
   (1) fails -- zero redirection risk, and useful later for testing
   `litisdoc`/`encrypta3` on macOS too if that's ever revisited.

Assessed risk of leaving the checkbox unmarked: low. No maintainer has
asked about macOS specifically in any review comment so far; Diego already
posted a transparent explanation on the PR. Not worth chasing unless a
maintainer explicitly asks for it.

## 5. PR #3764 status as of today

Open, `CHANGES_REQUESTED` (stale status -- doesn't auto-update from replies,
this is normal), mergeable. All ~20 review comments from `frankmorgner`/
`Jakuje` have real-code replies. Last maintainer activity was a review at
2026-08-08T02:31 UTC (bundled 4 inline comments, addressed same day).
Diego's last substantive reply was 2026-08-08T16:43 UTC. Per established
project etiquette (see the git history / earlier handoffs), don't ping
again until roughly a week of silence has passed.

## 6. Memory saved this session

A cross-session memory was saved: Diego expects rigorous, multi-path
empirical verification (checksums, protocol traces, every real downstream
consumer) before anything is reported as "tested and working," not a
single happy-path check. See
`~/.claude/projects/-home-diego-Documentos-starsign-driver-en/memory/verify-before-claiming-tested.md`.
