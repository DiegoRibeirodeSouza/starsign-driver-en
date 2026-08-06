# Research and Attempt Log: PJe 2.1 and PJeOffice

This document records all the approaches and architectures attempted to make electronic signing work in the PJe 2.1 ecosystem on Linux, circumventing the need to use the problematic proprietary drivers natively.

## 1. Approach: Emulation via `pje_headless`

**Objective:** Modify the `pje_headless` project (in Go) to intercept calls from the PJe 2.1 Angular frontend and return signatures created by the user's Open Source driver (`opensc-pkcs11.so`).

**What was done:**
- Login via Keycloak (Quarkus) worked perfectly by receiving pure JSON and validating the certificate.
- When trying to sign documents, PJe called legacy JSF Seam endpoints (e.g., `arquivoAssinadoUpload.seam`).
- The `pje_headless` code was adapted to recognize URLs ending in `.seam` and convert the payload from `application/json` to `application/x-www-form-urlencoded`.
- The `Authorization` header was properly propagated.

**The Problem (Technical Blocker):**
- Despite correctly sending the form, the PJe backend returned the error: `Erro:A assinatura do arquivo não foi fornecida!` (Error: The file signature was not provided!).
- **Root Cause Diagnosis:** PJe 2.1 uses Keycloak for cross-domain authentication, but the old monolithic application relies heavily on the `JSESSIONID` cookie to maintain the Conversation (the `cid=673752` parameter in the URL).
- The Angular frontend sends a JSON envelope to PJeOffice containing the `sessao` key. However, since the `JSESSIONID` cookie usually has the `HttpOnly` flag, the browser's Javascript (Angular) **cannot read it**. Consequently, the envelope reaches `pje_headless` without the vital cookie.
- Without the `JSESSIONID`, the POST request from `pje_headless` to the `.seam` endpoint arrives stateless. The backend cannot find the temporary file in memory (which was tied to the user's session) and triggers the error that the signature was not provided.

---

## 2. Approach: Isolation (Sandbox) via Distrobox

**Objective:** Abandon `pje_headless` and use the official Java PJeOffice, but isolate the problematic proprietary driver (`libaetpkss.so`) in a container so it does not pollute/conflict with the user's Debian ecosystem (which uses the OpenSC driver).

**What was done:**
- Distrobox (identified through the LinuxToys project) was used to create a "cage" based on Debian 12 (`pje_sandbox`).
- Java (`default-jre`) and the latest version of the PJeOffice installer (v2.5.16) were installed in the container.
- The proprietary driver file was moved to a persistent path on the host and accessed by the container.
- The PJeOffice shortcut was exported to the host Debian menu, making integration seamless.
- **Validated Architecture:** `pcscd` runs on the host managing the hardware and allows multiplexing. The Sudo/Host uses OpenSC and the Container/Java uses the Proprietary driver in parallel and simultaneously without USB port conflicts.

**The Problem (Technical Blocker):**
- When running, PJeOffice in the cage only accepted the proprietary driver (as expected), but the SafeSign proprietary driver ecosystem proved to be fundamentally flawed/unstable in the Linux version, making the signing operation impossible even within the "perfect" environment of the container.
- Seeing that the defect resided in the actual binary/stack of the court's proprietary driver, we opted for a **full rollback**, destroying the Distrobox cage and removing the shortcuts.

---

## Current Conclusion
The current PJe 2.1 architecture makes signing emulation via pure *headless* immensely difficult due to the mix of modern REST APIs (Quarkus) with old stateful endpoints (JSF/Seam) and session cookie blocks (`HttpOnly`). On the other hand, the Linux container route exposes the chronic problems of the proprietary drivers provided for Linux.

**Possible next steps in the future:**
- (For the Headless route) Create a browser extension (Chrome/Firefox) that has permission to read `HttpOnly` cookies and inject them into the request forwarded to `pje_headless`.
- (For the Sandbox route) Use "WinBoat" (Linux Subsystem for Windows) to create a Docker container running *Windows*, and thus install PJeOffice and the Proprietary driver in their Windows versions (which are vastly superior and more stable).
