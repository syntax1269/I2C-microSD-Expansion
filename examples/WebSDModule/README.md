# WebSDModule

An ESP32 Wi-Fi file explorer backed by the module. Open the serial console to
read the URL the board obtained via DHCP, then browse to it in a web browser.

Features:

- list files and directories on the card,
- upload files from your browser,
- download files, delete files,
- view card info, free space, die temperature, and the module version.

Edit the `SSID` and `PASS` constants near the top of the sketch to join your
network, then upload. This version uses synchronous handlers, so a long download
blocks other requests; use `WebSDModuleAsync` if you need concurrency.
