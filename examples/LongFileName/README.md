# LongFileName

Demonstrates long filenames (up to 259 characters) and nested directories.

The sketch creates a directory tree with `mkdir()`, writes files with long names
using `openWrite()`, lists the directory with `listDir()`, and reads the entries
back. It shows the module's path semantics: `/` separator, `DIR/FILE` and
`/DIR/FILE` equivalent, and a trailing `/` meaning a directory.

Note: the Minimal and Tiny builds use short (8.3 or 31-character) names. This
example needs the default full build.
