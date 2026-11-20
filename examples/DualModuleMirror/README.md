# DualModuleMirror

Two modules on the same I²C bus — `moduleA` at `0x6E`, `moduleB` at `0x6C` —
each with its own file handle and scratch buffer. Every frame is appended to
both cards; if Card A reports a `stickyError()` the sketch stops using it and
continues logging to Card B without dropping frames.

**Provisioning (once):** both modules ship at `0x6E`. This sketch can do it:

1. **Disconnect module A** from the I²C bus. This is essential — the address
   command changes *every* module currently at `0x6E`.
2. Uncomment `#define PROVISION_MODULE_B` near the top of the sketch, upload,
   and open the serial monitor. Type `Y` and Enter within 10 seconds.
3. The sketch sends `I2C_SET_ADDR (0x16) = 0x6C` and `SAVEEEPROM (0x1A)`.
4. **Reconnect module A**, comment the define out again, and reset. Both
   modules are now at `0x6E` and `0x6C` respectively.

You can also provision with the Qwiic Python port's `change_address()`. Never
run the address command with both modules attached.

Do not build with `I2CMICROSD_GLOBAL_STATE=1` — per-instance state is required.
