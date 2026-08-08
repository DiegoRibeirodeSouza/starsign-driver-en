# Windows Testing Handoff

Status: **complete and successful**. The StarSign CUT S driver builds and works
correctly on native Windows (MSVC), against physical hardware, validated
against real judicial platforms and Brazil's official signature-conformance
validator.

This document replaces an earlier handoff file that was lost to filesystem
corruption on the removable drive it lived on before these notes were
rewritten from scratch.

---

## 1. Build environment

Toolchain, matching the official `win32/` build path used by upstream CI
(`.github/workflows/windows.yml`):

- **Visual Studio Build Tools 2022** (C++ workload + Windows 11 SDK) — provides
  `cl.exe`, `link.exe`, `nmake.exe`.
- **vcpkg**, triplet `x64-windows-static` — provides static `openssl` and
  `zlib` (`libcrypto.lib`, `libssl.lib`, `zs.lib`). Set `VCPKG_INSTALLED`
  to the `vcpkg\installed` directory before invoking `nmake`; `win32/Make.rules.mak`
  auto-detects the libs from there.
- **CPDK** (Cryptographic Provider Development Kit) — installed via
  Chocolatey (`choco install windows-cryptographic-provider-development-kit`),
  needed for the minidriver (`ENABLE_MINIDRIVER` is unconditionally on in
  `win32/Make.rules.mak`, so it's required even for a "just the PKCS#11
  module" build).

Build command, from the `OpenSC/` directory, inside a `vcvarsall.bat x64`
environment:

```bat
set VCPKG_INSTALLED=C:\vcpkg\installed
nmake /nologo /f Makefile.mak all
```

(`nmake ... opensc.msi` additionally needs the WiX Toolset; not required for
building/testing the driver itself.)

## 2. Bug found: driver never compiled on Windows

`card-starsign.c` and `pkcs15-starsign.c` were added to `Makefile.am` (the
Unix/autotools build) but **never to `src/libopensc/Makefile.mak`** (the
win32/nmake build, maintained as a separate file/OBJECTS list). Consequence:
`opensc.dll` failed to link —

```
ctx.obj : error LNK2001: unresolved external symbol sc_get_starsign_driver
pkcs15-syn.obj : error LNK2001: unresolved external symbol sc_pkcs15emu_starsign_init_ex
```

— which cascaded into `opensc_a.lib` and `opensc-pkcs11.dll` failing to build
in downstream directories too (`pkcs11-tool.exe`/`pkcs15-tool.exe` failed to
link for an unrelated-looking reason: `pkcs11-display.obj` ends up compiled
into the wrong directory by nmake's batch inference rule when its "real"
location — `src/pkcs11/pkcs11-display.obj` — never got built due to the
`opensc_a.lib` failure upstream).

**Fix:** add `card-starsign.obj` and `pkcs15-starsign.obj` to the `OBJECTS`
list in `src/libopensc/Makefile.mak` (two-line change). Pushed directly to
the PR #3764 branch (`DiegoRibeirodeSouza/OpenSC@feature/starsign-cut-s-driver`,
commit `06663b372`) and applied here too.

## 3. Test results

### Token detection (`opensc-tool`)

```
opensc-tool -l    # -> "Giesecke & Devrient GmbH StarSign CUT S 0" detected
opensc-tool -n    # -> "G&D StarSign CUT S" (ATR match)
```

Windows recognizes the token's built-in CCID reader out of the box via the
generic Microsoft USB CCID class driver (`Microsoft Usbccid Smartcard Reader
(WUDF)`) — no vendor driver needed.

### PKCS#15 structure (`pkcs15-tool -D`)

All 4 certificates, both PINs (User/SO), and both RSA private/public key
pairs read correctly — output identical in shape to the Linux-validated
structure.

### PKCS#11 module (`opensc-pkcs11.dll`)

- `pkcs11-tool -L` / `-O`: slots, certs, pubkeys all list correctly.
- Real `C_Sign` against the current signing key: `CKR_OK`, 256-byte RSA-2048
  signature, card accepts with `SW 90 00`.
- **Caveat found:** `pkcs11-tool --test`'s full-token object enumeration
  (walks an orphaned pre-2024 key with no matching cert, then all 4 certs)
  leaves the wrong DF selected by the time it reaches the real signing key,
  and the card rejects the sign with `SW 6985` (Conditions of use not
  satisfied). This is **not** a driver bug in the real usage path — the card
  reuses the same raw key reference (`Key ref: 1`) for multiple keys,
  disambiguated only by which DF is currently selected. Isolating the sign
  call to the one real key (`pkcs11-tool --sign --id <hex> ...`) works
  cleanly.

### Windows minidriver (`opensc-minidriver.dll`)

Builds cleanly against the CPDK headers with no code changes beyond the fix
in §2 — the minidriver itself is generic/ATR-driven and has no per-card logic
to add.

### Real-world authentication

Successfully authenticated with the token, through the Windows build, on
three separate Brazilian judicial platforms:

- **TRT 3** (Regional Labor Court) — via `pje_headless`.
- **TJMG / PJe** (state court) — via `pje_headless`.
- **e-Proc** (federal court system) — talks to the token/minidriver
  **directly**, does not go through `pje_headless`.

`pje_headless` (companion Go service, `github.com/DiegoRibeirodeSouza/pje_headless`)
was built with `CGO_ENABLED=1` using MinGW-w64 `gcc` (required for the
`miekg/pkcs11` cgo binding), pointed at `opensc-pkcs11.dll` via
`PJE_PKCS11_MODULE` and `PJE_SIGNER_PRIORITY=pkcs11`.

### PDF signing (pyHanko)

`test_pdf_sign.py` in this repo signs a real PDF using
[pyHanko](https://github.com/MatthiasValvekens/pyHanko)'s PKCS#11 signer
against `opensc-pkcs11.dll`.

**Gotcha:** the driver only advertises `SC_ALGORITHM_RSA_HASH_NONE` (see the
main README's DigestInfo section). pyHanko's *default* mechanism selection
for RSA (`CKM_SHA256_RSA_PKCS`, hash done on-token) fails with
`pkcs11.exceptions.MechanismInvalid`. Fix: construct `PKCS11Signer` with
`use_raw_mechanism=True`, so pyHanko hashes client-side, builds the
DigestInfo block itself, and sends it to the card via plain `CKM_RSA_PKCS` —
exactly mirroring what `sc_pkcs15_compute_signature` already does internally
on the OpenSC side.

The resulting signed PDF (a real, multi-page court petition) was checked
against **Verificador de Conformidade / ITI** — the official Brazilian
government signature-conformance validator, published by ITI (Instituto
Nacional de Tecnologia da Informação, the federal agency that operates
Brazil's root PKI, ICP-Brasil). Result:

- `Status de assinatura: Aprovado` (signature status: approved)
- `Caminho de certificação: Valid` (full chain built up to the ICP-Brasil root)
- `Cifra assimétrica: Aprovada`
- `Resumo criptográfico: true`
- `Estrutura: Em conformidade com o padrão`

This is the strongest available signal that the Windows build produces
genuinely standards-compliant, court-acceptable signatures — not just a
signature that *pyHanko itself* considers internally consistent.

## 4. Upstream status

PR [#3764](https://github.com/OpenSC/OpenSC/pull/3764) checklist updated:
"Windows minidriver is tested" checked off, with a full write-up of the
above posted as a PR comment. Only "macOS token is tested" remains open
(no access to macOS hardware).

## 5. Reproducing the PDF signing test

```powershell
python test_pdf_sign.py path\to\some.pdf
```

Edit `TOKEN_LABEL` and `CERT_ID` at the top of the script to match your own
token (`pkcs11-tool --module OpenSC\src\pkcs11\opensc-pkcs11.dll -L` and
`-O`). Never overwrites the input file; writes `<input>_ASSINADO.pdf` next to
it by default.
