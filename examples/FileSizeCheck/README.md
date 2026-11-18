# FileSizeCheck

Shows how to query a file's size **without opening it**, then open it and read
exactly that many bytes.

After uploading, the sketch writes a small known file, calls `fileSize()` to get
its length, opens it with `openRead()`, reads that many bytes, and confirms the
data matches. It demonstrates `FILE_SIZE` / the `STATUS.result` latch used
behind the scenes — useful when you want to size a buffer before reading.

See [File Operations](../../wiki/File-Operations.md) for the raw-command
equivalent.
