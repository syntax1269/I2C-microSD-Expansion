# ConfigSaveLoad

Boots a small INI-style settings file from `CONFIG.INI` and demonstrates the
checked query overloads: `exists(path, &present)` and `fileSize(path, &size)`
distinguish "missing" from an I²C/card error.

Updates are staged: the new content goes to `CONFIG.TMP`, the live file is
removed, then the staged bytes are rewritten as `CONFIG.INI`. The module has no
atomic rename, so the config carries a checksum — a partial file is detected
at boot and replaced with defaults instead of being acted on.

Full builds only (`remove()` and `fileSize()` are compiled out in minimal
builds).
