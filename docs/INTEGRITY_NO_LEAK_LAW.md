# THE LAW — Integrity & No Data Collection (binding)

## Statement

1. **Grokium must not be usable as a data-collection product.**  
   Telemetry, usage analytics, crash-cloud, silent codebase upload, and
   off-box session prose are **forbidden**. Enabling them is a **law break**.

2. **Real-time share is StateMatrix only.**  
   Streaming = 0/1 lattice frames (+ algocube digit + integrity bits).  
   **No chat, no transcripts, no session bodies** on the stream.

3. **Nanobot integrity core (`nb-integrity`)** continuously verifies:
   - privacy flags remain hard-false
   - outbound destinations are allowlist-only
   - commander seal of integrity policy is valid
   - no prose leak channels are open

4. **Tampering is fail-closed.**  
   Broken seal / flipped telemetry / unknown egress → **DENY** operations,
   raise integrity SMX alert bits, optionally separate non-integrity bots.

5. **Modification for collection is made extremely difficult** by layered checks:
   sealed config hash · code hash set · commander-signed policy · runtime
   allowlist · SMX-only stream · integrity nanobot. All must agree.

## Creed

**No collection. StateMatrix streams. Integrity core watches. Leak = fail closed.**
