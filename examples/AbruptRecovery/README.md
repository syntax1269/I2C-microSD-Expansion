# AbruptRecovery

Shows what happens when the host disappears in the middle of a write.

The module watches the bus: after 5 seconds of silence it flushes pending bytes,
closes the open file, and advances its `opDone` counter on its own. This sketch
starts a write, deliberately stops talking to the module, then reconnects and
reads STATUS to resynchronize. The file is left consistent rather than corrupt.

The library's close barrier is gated on a >4 s activity gap so the first wait
after a crash is never mis-tracked. See
[Status and Polling](../../wiki/Status-and-Polling.md) for the mechanism and the
raw-driver equivalent.
