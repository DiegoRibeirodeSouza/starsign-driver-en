# Captured APDUs and PKCS#11 Call Sequence — G&D StarSign CUT S

**Date:** 2026-08-03  ✅ PHASE 1 COMPLETE
**Hardware:** G&D StarSign CUT S | VID:1059 PID:0019 | Serial: 001F00310028E510

> ⚠️ This file does not contain the PIN. The raw log (pkcs11spy_output.txt) was deleted
> after the necessary information was extracted.

---

## 1. ATR

```
3B F9 96 00 00 81 31 FE 45 53 43 45 37 20 0E 00 20 20 28
```

- TS: `3B` (Direct convention)
- OS: **StarSign Card Engine 7 (SCE7)** — G&D JavaCard
- Protocol: **T=1** (pure CCID)
- Historical bytes: `FE 45 53 43 45 37 20 0E 00` = "...SCE7..."

---

## 2. Token Identification (C_GetTokenInfo)

| Field | Value |
|---|---|
| label | `DIEGO RIBEIRO DE SOUZA` |
| manufacturerID | `A.E.T. Europe B.V.` |
| model | `19C43A06010D0000` |
| serialNumber | `001F00310028E510` |
| slotID | `0xcd01` (52481) |
| ulMinPinLen | 4 |
| ulMaxPinLen | 15 |
| flags | CKF_RNG, CKF_LOGIN_REQUIRED, CKF_USER_PIN_INITIALIZED, CKF_TOKEN_INITIALIZED |

**Note:** SafeSign creates 5 virtual slots (52481–52485) — only slot 0xcd01 has a token present.

---

## 3. Full PKCS#11 Call Sequence

### Initialization
```
C_GetInterface("PKCS 11")     → CKR_OK
C_Initialize(NULL)            → CKR_OK
C_GetSlotList(tokenPresent=0) → 5 slots: [52481, 52482, 52483, 52484, 52485]
C_GetSlotList(tokenPresent=0) → same (second call to get pointers)
C_GetSlotInfo(0xcd01)         → slot with token present
C_OpenSession(0xcd01, flags=CKF_SERIAL_SESSION=0x4) → hSession=0xcd0101
C_GetTokenInfo(0xcd01)        → see table above
```

### Authentication
```
C_Login(hSession=0xcd0101, userType=CKU_USER, PIN=[PIN bytes]) → CKR_OK
```
- PIN format: **ASCII bytes of the typed PIN, no padding**
- PIN length: variable (min 4, max 15 bytes)

### Object Enumeration
```
C_FindObjectsInit(hSession, pTemplate=NULL, ulCount=0)  ← EMPTY template = list everything
→ CKR_OK

loop:
  C_FindObjects(hSession, ulMaxObjectCount=1) → Object handle
  C_GetAttributeValue(hObject, CKA_CLASS)     → object type

  if CKO_DATA:
    C_GetAttributeValue(CKA_LABEL)
    C_GetAttributeValue(CKA_APPLICATION)
    C_GetAttributeValue(CKA_OBJECT_ID)
    C_GetAttributeValue(CKA_MODIFIABLE)
    C_GetAttributeValue(CKA_PRIVATE)

  if CKO_CERTIFICATE:
    C_GetAttributeValue(CKA_CERTIFICATE_TYPE)
    C_GetAttributeValue(CKA_LABEL)
    C_GetAttributeValue(CKA_SUBJECT)    ← DER-encoded DN
    C_GetAttributeValue(CKA_SERIAL_NUMBER)
    C_GetAttributeValue(CKA_ID)         ← SHA1 of the Subject Public Key
    C_GetAttributeValue(CKA_UNIQUE_ID)  ← returns CKR_ATTRIBUTE_TYPE_INVALID (ignore)

  if CKO_PUBLIC_KEY:
    CKA_KEY_TYPE, CKA_LABEL, CKA_ID, CKA_MODULUS_BITS, CKA_LOCAL, CKA_ENCRYPT, etc.

  if CKO_PRIVATE_KEY:
    CKA_KEY_TYPE, CKA_LABEL, CKA_ID, CKA_SENSITIVE, CKA_EXTRACTABLE, etc.

  C_GetTokenInfo()  ← SafeSign calls this after EVERY object (unnecessary overhead — do NOT replicate)
until C_FindObjects returns ulObjectCount=0

C_FindObjectsFinal()
```

**Critical note:** SafeSign calls `C_GetTokenInfo` after every enumerated object.
This is inefficient and contributes to heavy token load. **Our driver will NOT do this.**

### Finalization
```
C_Finalize() → CKR_OK
```

---

## 4. Objects on the Token

### Data Objects (CKO_DATA) — handle 0x1 to 0x7
| Handle | Label | Private |
|---|---|---|
| 0x1 | `keepass.keyx` | yes |
| 0x2 | `encryption key` | yes |
| 0x3 | `SHA-256` | yes |
| 0x4 | `SHA-256_1` | yes |
| 0x5 | `SHA-256_2` | yes |
| 0x6 | `SHA-256_3` | yes |
| 0x7 | `SHA-256_4` | yes |

### X.509 Certificates (CKO_CERTIFICATE)
| Handle | Label | ID (hex) |
|---|---|---|
| 0x8 | AC OAB G3 issued by AC Certisign G7 | `6A3BCF3A BF73E203 AE24CF9F 02CD30F1 2FFE7E66` |
| 0xf | ACRB v5 issued by ACRB v5 (Root) | `4ACADAB1 4B74BF4F BA7BACE6 4B91801C 44B8CC66` |
| 0x11 | AC Certisign G7 issued by ACRB v5 | `EB1046EA 1980E7E0 B2E95B07 29280A5D 17AD8E28` |
| 0x13 | **DIEGO RIBEIRO DE SOUZA 2024-10-09** | `444945474f 205249424549524f...` (ASCII label) |

### Keys
| Type | Handle | Label | ID |
|---|---|---|---|
| RSA 2048 public | (see log) | `19602704` | `2CDA5770BA19033F` |
| RSA private (old, expired) | (see log) | `DIEGO RIBEIRO DE SOUZA (10/18/2021 ~ 10/17/2024)` | `2CDA5770BA19033F` |
| **RSA private (active)** | (see log) | *(no label)* | `444945474f...` (cert label in ASCII) |

---

## 5. Key ID Pattern

SafeSign uses the **certificate label as ASCII bytes** for the corresponding private key's ID.

```
Certificate: "DIEGO RIBEIRO DE SOUZA 2024-10-09 20:22:25"
Key ID: 444945474F2052494245... (= ASCII of the label above)
```

Our driver must replicate this behavior so that applications like PJeOffice
find the correct key when looking it up by the certificate's ID.

---

## 7. Digital Signature Flow (CAPTURED)

### C_SignInit
```
hSession  = 0xcd0101
mechanism = CKM_SHA256_RSA_PKCS  (0x00000040)
params    = NULL, size=0
hKey      = private key handle (first CKO_PRIVATE_KEY found)
```
- The `CKM_SHA256_RSA_PKCS` mechanism performs **SHA-256 hashing + RSA-PKCS#1 v1.5 signing** on the card
- The token receives the **raw data** (not a pre-computed hash)
- No additional parameters required

### C_GetAttributeValue(CKA_ALWAYS_AUTHENTICATE)
- Returns `CKR_ATTRIBUTE_TYPE_INVALID` — **ignore, not supported by StarSign**

### C_Sign
```
hSession         = 0xcd0101
pData            = raw data to sign (any size)
pSignature       = 256 bytes (RSA-2048)
```

### Full Signing Sequence
```
C_Initialize(NULL)
C_GetSlotList(tokenPresent=0) → [0xcd01, ...]
C_GetSlotInfo(0xcd01)
C_OpenSession(0xcd01, CKF_SERIAL_SESSION=0x4) → hSession
C_GetTokenInfo(0xcd01)
C_Login(hSession, CKU_USER, pin_bytes)         ← plain ASCII PIN, no padding
C_FindObjectsInit(hSession, template=[], count=0)
C_FindObjects(hSession, maxCount=1)             → hKey (private key handle)
C_FindObjectsFinal(hSession)
C_SignInit(hSession, CKM_SHA256_RSA_PKCS, hKey)
C_GetAttributeValue(hKey, CKA_ALWAYS_AUTHENTICATE) → ignore error
C_Sign(hSession, data, &signature[256])
C_CloseSession(hSession)
C_Finalize()
```

### Notes for the driver
1. **Do not use CKM_RSA_PKCS** (which would require a pre-computed hash) — use `CKM_SHA256_RSA_PKCS`
2. **Do not replicate the C_GetTokenInfo** call after every object (SafeSign overhead)
3. **Plain ASCII PIN** — no 0xFF padding, no BCD
4. **Output is always 256 bytes** for RSA-2048

---

## 8. Status — What we have to implement the driver

| Component | Status | Detail |
|---|---|---|
| C_Initialize / C_Finalize | ✅ Mapped | trivial |
| C_GetSlotList / C_GetSlotInfo | ✅ Mapped | 5 virtual slots, 0xcd01 has the token |
| C_GetTokenInfo | ✅ Mapped | all fields documented |
| C_OpenSession / C_CloseSession | ✅ Mapped | flags=0x4 |
| C_Login / C_Logout | ✅ Mapped | plain ASCII PIN, CKU_USER |
| C_FindObjectsInit / C_FindObjects / C_FindObjectsFinal | ✅ Mapped | empty template = list everything |
| C_GetAttributeValue | ✅ Mapped | 8+ attributes per object |
| C_SignInit | ✅ Mapped | CKM_SHA256_RSA_PKCS, hKey |
| C_Sign | ✅ Mapped | raw data → 256 bytes |
| C_GetMechanismList / C_GetMechanismInfo | ⏳ To estimate | CKM_SHA256_RSA_PKCS minimum |
| C_GetFunctionList | ✅ Trivial | returns pointer table |

The captured log covers: initialization, login, and object listing.
Still need to capture:

```
C_SignInit(hSession, pMechanism={CKM_RSA_PKCS or CKM_SHA256_RSA_PKCS}, hKey)
C_Sign(hSession, pData=[SHA-256 hash], pSignature)
```

To capture this, run:
```bash
# Generate test data
echo "test" > /tmp/teste.txt

PKCS11SPY=/usr/lib/safesign-private/libaetpkss.so.3 \
PKCS11SPY_OUTPUT=~/Documentos/starsign-driver/research/spy_sign.txt \
pkcs11-tool --module /usr/lib/x86_64-linux-gnu/pkcs11-spy.so \
  --sign --mechanism SHA256-RSA-PKCS \
  --input-file /tmp/teste.txt \
  --output-file /tmp/assinatura.bin \
  --login
```

⚠️ Delete `spy_sign.txt` immediately after extracting the information.
