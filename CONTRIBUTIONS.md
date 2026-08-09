# Contribution History (Upstream)

This document tracks the official upstream contributions derived from the reverse engineering of the G&D StarSign CUT S native driver.

## 1. OpenSC (Native Driver)
- **Date:** August 2026
- **Status:** Pull Request Opened (Pending Review)
- **Official Link:** [PR #3764 - feat: Add native driver for G&D StarSign CUT S](https://github.com/OpenSC/OpenSC/pull/3764)
- **Related Issue:** Fixed Issue #2580 (*Giesecke & Devrient GmbH StarSign CUT S Unsupported card*) which had been open for two years.
- **Description:** The `card-starsign.c` driver source code developed in this repository was extracted, adapted, and submitted directly to the global OpenSC project. Once merged, this will allow all Linux distributions worldwide to recognize the token natively via `opensc-pkcs11.so`, entirely circumventing the proprietary SafeSign middleware.

## 2. Debian Bug Tracker
- **Bug Investigated:** [#1125519 - GnuTLS / SafeSign IC module incompatibility](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1125519)
- **Action:** No direct email action was required.
- **Reason:** This specific bug (opened Jan 2026) addressed a PKCS#11 initialization-flags conformance error within GnuTLS when handling the flawed proprietary driver (`libaetpkss.so`) -- SafeSign rejected initialization with certain thread-related flags set, but worked when they were zeroed. The GnuTLS community patched it independently with a fallback (merged in 3.8.12, upstream MR #2049 by Daiki Ueno) that is unrelated to this project. Our contribution here is orthogonal, not a cascade of that fix: by replacing SafeSign entirely with the native OpenSC driver at the hardware level, `libaetpkss.so` is never loaded in the first place, which makes this specific GnuTLS/SafeSign incompatibility moot for anyone who migrates to our driver -- not because our fix propagates into GnuTLS, but because the buggy proprietary module it works around is no longer in the picture. The Debian ticket had already been properly fixed, archived, and locked (fixed in 3.8.12-1, archived March 2026) before we looked into it, so no direct action on our part was needed.
