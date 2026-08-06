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
- **Reason:** This specific bug (opened in Jan 2026) addressed a thread locking error within GnuTLS when handling the flawed proprietary driver (`libaetpkss.so`). The GnuTLS community patched it internally with a fallback. Since our solution tackles the root cause at the hardware level and replaces the middleware via OpenSC, the fix will cascade to Debian automatically via the official OpenSC packages. Furthermore, the Debian ticket had already been properly archived and locked.
