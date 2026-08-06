# Step-by-Step: Reverse Engineering the G&D StarSign CUT S

This document details the methodological process used to break through the barriers of the proprietary SafeSign middleware and build the native open-source driver for OpenSC.
The goal of this guide is to prove that reverse engineering cryptographic hardware does not require complex binary decompilation (like using Ghidra or IDA Pro), but rather **intelligent interception and comparative traffic analysis (APDU Sniffing)**.

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

---

## 2. Analyzing Traffic and Discovering the Locks

When we opened the generated file and compared it with the standard behavior documented by the ISO-7816 standard, we noticed three major differences.

### Discovery A: The "Attestation Handshake" (Licensing Lock)
Right after initialization (ATR), we saw that the proprietary driver sent a strange APDU, class `00 DA 01 00` (a `PUT DATA` command).
When we converted the payload from hexadecimal (`49 20 61 6d...`) to ASCII, the message was clear:
```text
I am A.E.T. Europe B.V. SafeSign or BlueX approved software.
```
**The Problem:** Without receiving exactly this "password" phrase in plain text, the StarSign applet ignored the next file selection commands. The token literally demanded to be greeted by the name of the original software.

### Discovery B: Hidden Logical Channels
The ISO-7816 standard operates on channel `0` (indicated by the first byte of the APDU, the `CLA = 00`).
We observed in the log that SafeSign sent the command:
```text
00 70 00 00 01
```
This is a `MANAGE CHANNEL` asking to open an alternative logical channel. From that moment on, all captured APDUs from SafeSign started with `01` (e.g., `01 A4...`). The applet forced communication to occur on an isolated channel (Channel 1).

### Discovery C: The Bizarre `SELECT FILE` Anomaly
When OpenSC (generic driver) tries to read a file on the smartcard, it sends the standard `SELECT` command: `00 A4 00 00`. The parameter `P2=00` means: *"Select the file and return the FCI header with the file size"*.
The G&D StarSign responded to this with silence or an error.

Observing the SafeSign log, we noticed that it always sent `P2=0x0C` (`01 A4 00 0C`). In the ISO-7816 protocol, `0C` means *"Select the file, but do not respond to me (No FCI expected)"*.
**The Problem:** Because the token does not respond with the file size, OpenSC's standard function assumes the file is **0 bytes**, making it impossible to read the certificates.

---

## 3. Engineering in Action: Writing the Driver in C

With the three secrets in hand, the solution was to write a driver (we called it `card-starsign.c`) embedded in OpenSC that mimicked this behavior.

1. **Injecting the Greeting (Handshake):**
   In the `starsign_init` function, we force the APDU `00 DA 01 00` containing the magic ASCII string, firing it twice (as seen in the logs) before anything else.
2. **Forcing the Logical Channel:**
   Right after the greeting, we send the channel opening (`00 70 00 00 01`) and internally fix in OpenSC that all subsequent conversations must use the channel prefix `card->cla = 0x01`.
3. **Tricking OpenSC on `SELECT FILE`:**
   We created the custom function `starsign_select_file`. In it, we force all selections to use `P2=0x0C`. And for the "0 size" problem? We injected an elegant workaround: we tell OpenSC that the file size is `0x8000` (huge). OpenSC's low-level intelligence (`sc_read_binary`) starts reading continuously until it hits the real end of the file, overcoming the lack of FCI.
4. **Customization of PIN and Signature:**
   With the read channels ready, we saw that the MSE (Manage Security Environment) command was also unique (`84 01 01 80 01 02`). We override `starsign_set_security_env` and forced a 15-byte padding on the PIN submission (`starsign_pin_cmd` with `P2=0x02`).

---

## 4. The Final Hurdle: The RAW Algorithm and the Death of Java 8

Even with the driver reading perfectly, **PJe Office** (from the Brazilian judicial system) refused to sign.
We discovered, by generating more signature logs in the terminal (`pkcs11-tool --sign`), that the StarSign hardware rejected pre-formatted (padded) hashes.

- **The Solution in the Driver:** We injected the `SC_ALGORITHM_RSA_RAW` flag in C, which makes the token receive the raw hash, letting OpenSC just handle the PKCS#1 padding.
- **The External Factor (PJe):** We discovered that the underlying Java 8 language of the official PJe Office *has no support* for RAW RSA signatures via Smartcards. When it fails, BouncyCastle tries to extract its Private Key from the hardware to sign via software. Since the token blocks this (`CKA_SENSITIVE`), PJe collapses.

**The Final Move:** We replaced the outdated Java-based PJe Office with the contemporary **`pje_headless`** solution written in Go, which communicated perfectly with our OpenSC driver, closing the reverse engineering cycle with a functional ICP-Brasil digital signature on native Linux, with absolutely nothing proprietary.
