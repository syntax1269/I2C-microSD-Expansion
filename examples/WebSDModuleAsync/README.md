# WebSDModuleAsync

An asynchronous variant of the ESP32 Wi-Fi file explorer. It uses the
`AsyncWebServer` library so one client streaming a large download does not block
other connections.

> **Security:** this demo has no authentication and is intended for a
> trusted LAN only. Dynamic filenames and `DIR` arguments are HTML-, URL-
> and JSON-escaped before rendering, but the endpoints themselves are open.

After editing the Wi-Fi credentials, upload the sketch and open the address
printed in the serial console. You can upload, download, list, and delete files
on the module's card, and watch live status/card/statistics cards in the page.

The async handlers are written to survive a client aborting a download mid-stream
— the sketch recovers and the card stays consistent. See
[Status and Polling](../../wiki/Status-and-Polling.md) for the commit semantics
that make this safe.
