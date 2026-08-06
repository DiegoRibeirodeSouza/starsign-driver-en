# Open A3 Driver (OpenSC Native Module)

![Version](https://img.shields.io/badge/version-0.1.0-blue.svg)
![License](https://img.shields.io/badge/license-LGPLv2.1-green.svg)
![Status](https://img.shields.io/badge/status-functional%20|%20awaiting%20upstream%20review-orange.svg)

This repository contains the research, reverse engineering, and final implementation of a native C driver for **OpenSC**, enabling the use of the **G&D StarSign CUT S (A3)** cryptographic token on modern Linux systems **without the need for the proprietary SafeSign middleware**.

## Motivation

This project was born out of real pain: the extreme difficulty of keeping the proprietary middleware from A.E.T. Europe (SafeSign) running on modern Linux distributions (such as Debian 13 and Ubuntu 24.04/26.04).
Installing the proprietary library (`libaetpkss.so`) required a real packaging engineering effort, dealing with broken dependencies of old versions of `libssl` and `libwxbase`. Beyond the dependency nightmare, we identified that continuous status checks (polling) caused the token to **abruptly shut down** due to communication bugs within the proprietary middleware.

The definitive solution? **Eliminate SafeSign and create a native, open-source driver for OpenSC.**

## Reverse Engineering and Technical Discoveries

The OpenSC project already supports dozens of smartcards natively, but the G&D StarSign CUT S had protection mechanisms and peculiarities in its ISO-7816 protocol implementation that prevented native reading. We began reverse engineering by intercepting USB and PC/SC calls (via `pcscd`), uncovering multiple barriers.

The technical analysis of the code (`card-starsign.c`) reveals the following essential innovations applied to OpenSC:

### 1. Initialization Attestation (Compatibility Handshake)
The token refused complex commands unless it was initialized with a textual attestation that the running software was official:
`I am A.E.T. Europe B.V. SafeSign or BlueX approved software.`
Our driver implements the exact injection of this plain text string via a `PUT DATA` command (APDU: `DA 01 00`) right after the card reset, unlocking access to the applet.

### 2. Logical Channels Management
The PKCS#15 applet refuses operations on the default communication channel (Channel 0). The driver sends a `MANAGE CHANNEL` command (`70 00 00`) to open a new logical channel (Channel 1), forcing all subsequent APDUs in the driver to use the class `CLA = 0x01`.

### 3. Selection Bypass and File System Peculiarity
The StarSign CUT S **silently rejects** the `SELECT FILE` instruction if the FCI control request is made in the traditional standard (`P2=00`).
Our empirical reverse engineering discovered that it only accepts `P2=0x0C` (No FCI response expected). Without the FCI, the native OpenSC saw all files with `size = 0`. To resolve this, we overrode the `starsign_select_file` function:
- We force `P2=0x0C`.
- We inject a large dummy size (`0x8000`) into the OpenSC structure. The OpenSC core (`sc_read_binary`) is smart enough to stop reading when it reaches the end of the actual file.
- We implemented an automatic fix for paths attempting to return to the MF (`3F00`), re-selecting the correct applet (`5015`) to avoid failures in relative token references.

### 4. Customization of Security Environments (MSE) and PIN
The original ISO7816 driver did not assemble the `Manage Security Environment` (MSE) APDU exactly as the chip required. We created an override in `starsign_set_security_env` to inject the specific bytes `84 01 01 80 01 02` for signing operations (`SC_SEC_OPERATION_SIGN`).
Similarly, we overrode the PIN transmission (`starsign_pin_cmd`) to force the `P2=0x02` parameter and apply a fixed 15-byte padding to the buffer.

## The Final Challenge: `NONEwithRSA` and PJe Office

After the initial success of reading the token, we faced the rejection of the signature by the Brazilian judicial system (**PJe Office**).

### The RAW Algorithm Restriction
To sign documents compatible with ICP-Brasil, the system requires a "raw" signature, without prior padding formatting via PKCS#11, known as `NONEwithRSA` / `CKM_RSA_X_509`. Tests showed that the G&D StarSign returned the error `67 00` if it received hashes already formatted in `C_Sign`.
In the C code of our driver, we activated the **`SC_ALGORITHM_RSA_RAW`** flag for the key sizes (1024, 2048, 4096). This enabled the token to receive raw hashes (RAW RSA) and allowed OpenSC to handle PKCS#1 padding in a compatible manner.

### The Java 8 and BouncyCastle Bottleneck
Even so, the **official PJe Office** crashed:
`InvalidKeyException: Supplied key (...) is not a RSAPrivateKey instance`

We discovered that the blame lied three layers away from the hardware:
1. PJe Office is an application built for **Java 8**.
2. The `SunPKCS11` provider in Java 8 **does not support `NONEwithRSA`** for smartcards (legacy bug).
3. Java 8 delegates the signature to the **BouncyCastle** fallback library.
4. BouncyCastle tries to extract the private key from the card to sign via software. Since the token enforces `CKA_SENSITIVE = TRUE` (unextractable key), the system collapses.

### The Solution: Integration with `pje_headless`
The final barrier wasn't our driver, but the legacy ecosystem. We replaced the official client with [**pje_headless**](https://github.com/MrSchrodingers/pje_headless) (written in Go), which removes the JVM dependency. The `pje_headless` communicates perfectly with our native OpenSC compilation (`opensc-pkcs11.so`), eliminating SafeSign once and for all. The use of tokens in PAM (sudo) was also successfully restored via the PKCS#11 module.

## Safe Installation

> [!WARNING]
> This driver is currently being submitted to the official OpenSC repository (upstream). Installing it by overwriting the system's default library may alter the behavior of non-StarSign tokens.

**Tested Requirements:**
- **OS:** Debian 13 / Ubuntu 24.04
- **Token:** G&D StarSign CUT S (ICP-Brasil A3)

### Option 1: Easy Installation (Recommended)
Go to the **Releases** tab on GitHub and download the pre-compiled `.deb` packages.
```bash
sudo apt install ./opensc*.deb
```

### Option 2: Manual Compilation
If you prefer to compile, use a custom prefix to isolate the driver:
```bash
cd OpenSC
./bootstrap
./configure --prefix=/opt/starsign-opensc --enable-pcsc
make
sudo make install
```

### How to use with PJe (TJMG / etc)
Due to the Java 8 bug with `NONEwithRSA`, **do not use the official PJeOffice**. Instead, use the excellent Go client, `pje_headless`.
Simply point the environment variable to our compiled driver (if you used the `.deb` packages, the library will be at `/usr/lib/x86_64-linux-gnu/opensc-pkcs11.so`):

```bash
export PJE_PKCS11_MODULE=/usr/lib/x86_64-linux-gnu/opensc-pkcs11.so
./pjeheadless
```

## Credits

- **OpenSC Community:** For the fantastic base framework for ISO-7816 and PKCS#15 communication.
- **MrSchrodingers / pje_headless:** For creating the Go client, which saved ICP-Brasil from the shackles of the legacy JVM.
- **Debian Project:** For providing the robust debugging tools and documentation (such as `pcscd`) that made the reverse engineering possible.

## License
This code modifies OpenSC and inherits its compatibility. The project and modifications are distributed under the **LGPLv2.1** license. (See the `LICENSE` file in the OpenSC repository).
