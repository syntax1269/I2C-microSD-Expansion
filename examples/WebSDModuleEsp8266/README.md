# WebSDModuleEsp8266

A slim Wi-Fi file browser for the ESP8266 (including the ESP-01S with its
limited pins and flash).

Edit the Wi-Fi credentials at the top of the sketch, select your ESP8266 board
and speed in PlatformIO (80 MHz or 160 MHz CPU options are provided), build and
upload. The serial console prints the board's address once it joins the network.

The browser lists files, uploads new ones, and downloads or deletes existing
ones. Directory listings are capped at 32 entries by default; raise
`MAX_ENTRIES` if you expect more files in one folder.
