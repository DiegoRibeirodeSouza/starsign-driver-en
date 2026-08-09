# Step-by-Step: Reverse Engineering the G&D StarSign CUT S

This document details the methodological process used to break through the barriers of the proprietary SafeSign middleware and build the native open-source driver for OpenSC.
The goal of this guide is to prove that reverse engineering cryptographic hardware does not require complex binary decompilation (like using Ghidra or IDA Pro), but rather **intelligent interception and comparative traffic analysis (APDU Sniffing)**.

> **A note on process, not just results:** some of the conclusions below were wrong on the first pass and only caught under closer scrutiny (peer review, or decrypting a live signature with the card's own public key to check it was actually valid). Where that happened, this guide says so explicitly, because the correction is as instructive as the original discovery.

---

## 1. The Interception Environment: "Sniffing"

When dealing with Smartcard tokens, all USB communication goes through a Linux daemon called `pcscd` (PC/SC Smart Card Daemon).
To discover how the proprietary driver talked to the token, we needed to "listen" to this conversation.

### Step 1: Capturing APDUs from the Proprietary Driver
1. Stop the official pcscd service running in the background:
   ```bash
   sudo systemctl stop pcscd.socket pcscd.service
   ```
2. Start pcscd in "foreground" mode with the magic `--apdu` flag, which spits out all USB traffic in hexadecimal to the screen, and save it to a file:
   ```bash
   sudo pcscd --foreground --apdu 2>&1 | tee pcscd_apdu_log.txt
   ```
3. In another terminal, use the `pkcs11-tool` to force the proprietary driver (`libaetpkss.so`) to log into the token:
   ```bash
   pkcs11-tool --module /usr/lib/libaetpkss.so --list-objects --login
   ```
4. Terminate `pcscd` by pressing `Ctrl+C`. The `pcscd_apdu_log.txt` file will contain the entire secret conversation.

> **Security note:** this log will contain the PIN in plain text if you authenticate during capture. Delete it once you've extracted what you need — don't let it linger in a repository. (We didn't, the first time. See `relatorio_testes_starsign.md` for the retrospective on that.)

---

## 2. Analyzing Traffic and Discovering the Locks

When we opened the generated file and compared it with the standard behavior documented by the ISO-7816 standard, we noticed several major differences.

### Discovery A: The "Attestation Handshake" (Licensing Lock)
Right after initialization (ATR), we saw that the proprietary driver sent a strange APDU, class `00 DA 01 00` (a `PUT DATA` command).
When we converted the payload from hexadecimal (`49 20 61 6d...`) to ASCII, the message was clear:
```text
I am A.E.T. Europe B.V. SafeSign or BlueX approved software.
```
**The problem:** without receiving exactly this "password" phrase in plain text, the StarSign applet refused later PKCS#15 operations. The token literally demanded to be greeted by the name of the original software — sent **twice**: once right after the ATR, and once again after the PKCS#15 AID is selected (the card apparently re-checks after the applet context changes).

### Discovery B: Hidden Logical Channels
The ISO-7816 standard operates on channel `0` (indicated by the first byte of the APDU, the `CLA = 00`).
We observed in the log that SafeSign sent the command:
```text
00 70 00 00 01
```
This is a `MANAGE CHANNEL` command asking to open an alternative logical channel. From that moment on, all captured APDUs from SafeSign started with `01` (e.g., `01 A4...`). The applet forced communication to occur on an isolated channel (Channel 1) for everything beyond the initial handshake.

### Discovery C: `SELECT FILE` — three separate anomalies, not one

This is the part that took the longest to get right, and the part where the first version of this driver got it wrong. There isn't one `SELECT FILE` quirk, there are three, layered on top of each other:

**C.1 — `P1`, not `P2`, is what the card is picky about.** The generic OpenSC driver tries `P1=0x01` ("select child DF") when descending into a directory. This card answers `SW 6A82` (file not found) for that. Every genuine SafeSign capture selects strictly by plain File ID (`P1=0x00`) — never by "child DF" or "child EF" semantics. Get this wrong and the entire filesystem looks empty.

**C.2 — a `0x3FFF` component in the path is a placeholder, not a real file.** Certificate and data-object paths look like `3F00 3FFF 4302 05A0`. The first version of this driver assumed everything between `3F00` and the final component had to be selected in sequence, including `3FFF` — and got `SW 6A82` for it. Comparing against a genuine SafeSign 4.7 capture settled it: **SafeSign itself never selects `0x3FFF`.** It's a fictitious placeholder baked into the path convention, not a file. The component right after it (`4302` in this example) *is* a real directory one level below the PKCS#15 application DF (`5031`) and must be selected — but re-selecting it a second time while it's already the current DF *also* fails with `6A82`, so a correct driver has to track "is this directory already selected?" and skip the redundant `SELECT` when it is.

**C.3 — the card doesn't return a usable file size, and guessing is a trap.** Early on, the fix for "OpenSC assumes a 0-byte file when it can't determine size" was to lie and claim every file is `0x8000` (32 KB) so `sc_read_binary`'s bounds check never rejects a legitimate read. **This works, but it's a hack, and an OpenSC maintainer correctly flagged it in review**: a hardcoded oversized placeholder just moves the bug — it happens to be harmless for small files and merely wasteful for a ~1.8 KB certificate, but it's fragile, and it's not what the card is actually telling you. The real fix is to parse the FCP (File Control Parameters) response the card *does* return on `SELECT` (tag `0x80`, the size field) and use that. OpenSC's standard ISO 7816 FCP parser (`iso_ops->process_fci()`) already does this correctly — the fix was to stop overriding the file object by hand and let that parser fill it in from the real APDU response.

---

## 3. Engineering in Action: Writing the Driver in C

With the anomalies understood, the solution took shape as a driver we called `card-starsign.c`, embedded in OpenSC:

1. **Injecting the greeting (handshake).** In `starsign_init()`, the APDU `00 DA 01 00` carrying the magic ASCII string is sent twice — once right after channel setup begins, once again after the PKCS#15 AID is (re-)selected — mirroring the two-handshake pattern seen in the capture.
2. **Forcing the logical channel.** Right after the second handshake, `MANAGE CHANNEL` (`00 70 00 00 01`) is sent, and `card->cla` is fixed to `0x01` for every subsequent APDU in the session.
3. **A real `SELECT FILE` state machine, not a blanket rule.** `starsign_select_file()` now branches on what's actually being selected: a genuine top-level MF→DF walk (plain `P1=0x00`, `P2=0x0C` selects, one FID at a time); a direct child EF of the already-active PKCS#15 DF (skips re-navigating from the MF entirely, since doing that would silently drop the "current DF" context the other branches depend on); and the `0x3FFF`-placeholder virtual path (select the placeholder's *next* component only, cache it, skip the reselect if it's already current). A small piece of per-card state (`struct starsign_drv_data`) tracks which DF is active and which "mid" directory was last selected, specifically so the driver doesn't re-issue a `SELECT` that the card would reject as redundant.
4. **Real file sizes from the card's own answer.** `starsign_select_ef_child()` hands the raw FCP bytes from the `SELECT` response to `iso_ops->process_fci()`, the standard ISO 7816 parser, instead of guessing. Verified against physical hardware: `pkcs15-tool -D` output is byte-identical to what the old hardcoded-size version produced, for every certificate, key, and PIN object — except it's now derived from what the card actually said, not assumed.
5. **Customization of Security Environments (MSE).** The generic ISO7816 driver didn't assemble the `MANAGE SECURITY ENVIRONMENT` APDU the way this chip requires. `starsign_set_security_env()` sends the exact byte sequence `84 01 01 80 01 02` (Key Reference before Algorithm Reference — the opposite tag order of what generic OpenSC produces) for `SC_SEC_OPERATION_SIGN`, reverse-engineered from the same genuine SafeSign capture.
6. **PIN handling lives in the PKCS#15 profile, not in `card-starsign.c` at all.** Earlier revisions of this driver had a hand-rolled `starsign_pin_cmd()` that forced 15-byte PIN padding and a hardcoded reference. That function is gone. The PIN reference (`0x02` for the User PIN, `0x01` for the SO PIN), its length limits, and its padding are now declared once in `pkcs15-starsign.c`'s static profile (see below), and the driver relies entirely on the standard, unmodified `iso7816` `pin_cmd()` implementation to do the actual `VERIFY`/`CHANGE`/`UNBLOCK` APDUs.

---

## 4. A Structural Rewrite: Static PKCS#15 Profile, Not Runtime Parsing

The card's own on-card PrKDF/AODF/CDF structures are well-formed enough that OpenSC's generic dynamic parser (`sc_pkcs15_parse_df()`) succeeds on its own — but the values it produces are awkward (a missing PIN reference, key IDs that are the cardholder's *name*, ASCII-encoded, taken straight from the certificate label). Patching those values after the fact, on every card session, made the `SELECT FILE` logic above harder to reason about than it needed to be.

Following review feedback and the pattern used by `pkcs15-esteid2025.c`, `pkcs15-starsign.c` instead supplies the PKCS#15 object layout (certificate/key/PIN paths, labels, usage flags) as **static internal data**, compiled into the driver — because that layout is fixed by the StarSign CUT S / SafeSign applet across every token of this model, not something that needs to be rediscovered per card. What genuinely varies per cardholder (token label, serial number, manufacturer ID) is still read from the card's own `EF(TokenInfo)` at runtime, so the driver works correctly for any StarSign CUT S holder, not just the one it was developed against.

One consequence worth calling out explicitly: the card's own CDF/PrKDF store each key/certificate's correlation ID as the cardholder's full name plus issuance date, ASCII-encoded (e.g. `"...2024-10-09 20:22:25"`). A static, compiled-in profile shared by every user of this driver obviously cannot hardcode one specific person's name — so the IDs used internally by the driver are small, arbitrary values (`01`, `02`, ...) that only need to be *consistent within a session*, not to match anything printed on anyone's card.

---

## 5. The Final Hurdle: Signatures That "Succeeded" But Weren't Valid

Getting `pkcs11-tool --sign` to return `CKR_OK` and the card to answer `SW 90 00` felt like the finish line. **It wasn't, and trusting that alone was the single biggest mistake of this project.**

### What actually happened
With `SC_ALGORITHM_RSA_HASH_SHA256` advertised, OpenSC hands the card a bare 32-byte SHA-256 digest and lets the card do the PKCS#1 v1.5 padding on-chip. The card accepts this and returns success. But decrypting a real signature produced this way, using the token's own public key, showed the padded block contained the raw digest with **no `DigestInfo` ASN.1/OID header** — the `30 31 30 0D 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20` prefix a standards-compliant SHA-256 signature requires. Every signature produced this way was cryptographically invalid, and would be rejected by any real conformance checker (Adobe, ITI, `pdfsig`), despite the card's `SW 90 00`.

### The fix
Advertise **only** `SC_ALGORITHM_RSA_HASH_NONE` (drop `SHA256`/`SHA1`/`MD5`). This forces OpenSC's own crypto layer to build the complete `DigestInfo` + PKCS#1 v1.5 block in software, and hand the card an already-padded, full-size blob for a raw modular exponentiation (`SC_ALGORITHM_RSA_RAW`). Verified for real this time: a PDF signed through [`litisdoc`](https://github.com/DiegoRibeirodeSouza/litisdoc) (via pyHanko's PKCS#11 raw-mechanism signer) validates as `Signature is Valid` under `pdfsig` (Poppler), and separately passes Brazil's official ITI conformance validator with a valid certification path to the ICP-Brasil root.

**The lesson:** `SW 90 00` proves the card accepted the command. It proves nothing about whether the cryptographic result is actually correct. Decrypt the signature with the public key and check the padding by hand before declaring victory.

---

## 6. A Hardware Ceiling You Cannot Fix in Software

Correcting the DigestInfo problem means a full 256-byte payload (not a 32-byte hash) has to reach the card for every raw RSA-2048 operation. On at least one StarSign CUT S unit, the token's *built-in* CCID reader intermittently fails that transfer with `SCARD_E_INVALID_PARAMETER`, and `pcscd` logs why plainly:
```
CmdXfrBlockTPDU_T0() Command too long (265 bytes) for max: 261 bytes
```
The reader's own USB CCID descriptor (`lsusb -v`) declares a firmware-fixed `dwMaxCCIDMsgLen` of 271 bytes (~261 usable after the CCID header), and does not advertise "Extended APDU level exchange" support. Even the leanest possible extended APDU carrying 256 bytes of data comes to 263 bytes — over the ceiling regardless of encoding — and ISO 7816-4 command chaining, the usual workaround, is rejected outright by this card (`SW 6E 00`). The only way around it would be the card hashing and padding on-chip *correctly* (so only a 32-byte hash crosses the wire) — which section 5 above already showed this hardware doesn't do.

**Practical effect:** raw RSA-2048 sign/decipher operations through this specific reader are not 100% reliable — they can fail intermittently right at that byte boundary and typically succeed on retry. This is a firmware limitation of the CCID controller, not something `card-starsign.c` can fix. See `relatorio_testes_starsign.md` §6 for the full investigation.

---

## 7. The Ecosystem Problem: `NONEwithRSA` and PJe Office

After the driver itself worked, signatures were still rejected by the Brazilian judicial system (**PJe Office**) — for reasons that had nothing left to do with the card.

### The RAW algorithm requirement
ICP-Brasil-compliant signing requires `NONEwithRSA` / `CKM_RSA_X_509` — a "raw" signature with no PKCS#11-side padding. This lines up with, and is satisfied by, the `SC_ALGORITHM_RSA_RAW` fix from section 5.

### The Java 8 / BouncyCastle bottleneck
Even so, the **official PJe Office** crashed with `InvalidKeyException: Supplied key (...) is not a RSAPrivateKey instance`. The chain of blame runs three layers deep:
1. PJe Office is built for **Java 8**.
2. Java 8's `SunPKCS11` provider does not support `NONEwithRSA` for smartcards (a known, long-standing bug).
3. Java 8 falls back to the **BouncyCastle** software implementation.
4. BouncyCastle tries to extract the private key from the card to sign in software. Since the token enforces `CKA_SENSITIVE = TRUE` (non-extractable key), it fails.

### The workaround
Replace the official PJe Office client with [**pje_headless**](https://github.com/MrSchrodingers/pje_headless), a Go client with no JVM in the loop, which talks to `opensc-pkcs11.so` correctly. Closes the loop with a fully functional, fully open-source ICP-Brasil signing stack on native Linux.

---

## Summary: what a from-scratch reproduction actually needs

If you're reverse engineering a different token using this same methodology, the ingredients that mattered here, roughly in the order they matter:
1. A genuine capture of the *proprietary* driver talking to the card — not just ISO 7816 documentation. Several of this card's requirements (tag order in MSE SET, the two-handshake DRM pattern, `P1=0x00` selects) are not standard behavior and would not have been guessed from the spec alone.
2. Willingness to say "this looks like it works" is not the same claim as "this is cryptographically correct" — verify signatures by decrypting them with the public key, not just by checking the status word.
3. Treat early workarounds (size placeholders, blanket P2 values) as provisional. They get you unblocked; they are not the final design, and review from people who know the codebase's conventions (here, OpenSC's own maintainers) is what turns a working hack into a maintainable driver.
