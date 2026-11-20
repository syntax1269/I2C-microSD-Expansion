I2C microSD Expansion — Arduino Library
=======================================

Add FAT file storage to any Arduino board with just two I²C wires. The
``I2C microSD Expansion`` module owns the SD card and the FAT filesystem; your
sketch opens files, writes and reads bytes, lists directories and queries card
health over plain ``Wire`` I²C. No FAT stack, SD driver or level shifter in your
code.

The host owns the bus (``Wire.begin()``), just like any other I²C device.

.. code-block:: cpp

   #include <Wire.h>
   #include <I2C_microSD_Expansion.h>

   I2CmicroSD microSDexp;

   void setup() {
     Serial.begin(115200);
     Wire.begin();
     if (!microSDexp.begin(0x6E, 400000)) {
       Serial.println(F("module not found"));
       while (1) delay(10);
     }
     File *f = openFile(&microSDexp, "HELLO.TXT", "w");
     if (f) {
       const uint8_t msg[] = "HELLO-WORLD";
       fileWrite(f, msg, sizeof(msg) - 1);
       fileClose(f);
     }
   }

   void loop() {}

Requirements
------------

* An Arduino board with a ``Wire`` I²C bus (AVR, ATtiny85 via TinyWireM,
  ESP32, ESP8266 and RP2040 are supported).
* An I²C microSD Expansion module with a FAT16/FAT32-formatted microSD card.
* Module firmware 18.5.x or newer for the file API, 18.5.2 for pins/bit-bang,
  and 18.8.0 for banner frames and the measured-clock report.

Installation
------------

Install ``I2C microSD Expansion`` from the Arduino Library Manager, or copy
this repository into your sketchbook's ``libraries/`` folder. The library is
zero-heap: fixed buffers, no ``String`` and no ``Serial`` output from the
library itself.

Design notes
------------

* One file can be open at a time. ``close()`` and ``sync()`` return only after
  the bytes are committed to the card.
* The library is not reentrant or interrupt-safe: use one instance from one
  call site. The single file handle and shared staging buffer are not
  protected against nested calls.
* Expansion-pin modes apply in RAM only. Call :cpp:func:`I2CmicroSD::savePINMode` once when
  your configuration is final to persist them to the module's EEPROM.

.. toctree::
    :caption: API Reference
    :maxdepth: 3

    api

.. toctree::
    :caption: Examples

    examples

.. toctree::
    :caption: Links

    Source repository <https://github.com/syntax1269/I2C-microSD-Expansion>
    Arduino Library Manager <https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-installing-a-library/>
    CircuitPython port <https://i2c-microsd-expansion-circuitpython.readthedocs.io/>
    Protocol reference <https://github.com/syntax1269/I2C-microSD-Expansion/blob/main/docs/protocol.md>
    Wiring guide <https://github.com/syntax1269/I2C-microSD-Expansion/blob/main/docs/wiring.md>

Indices and tables
------------------

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`

Building these docs locally
---------------------------

From the repository root::

    pip install -r docs/requirements.txt
    python -m sphinx -b html -W docs docs/_build/html

Blocking behavior
-----------------

Most high-level methods are **synchronous**: they poll the module's STATUS
frame with a short ``delay()`` until the operation completes or a timeout
expires. The I²C bus stays protocol-responsive, but your sketch/application is
blocked for the duration — plan for watchdogs, real-time loops, cooperative
schedulers and ESP32 tasks.

Timeouts scale up on slow buses and are capped at 120 s by the host.
``begin()`` takes ~1.5 s; ``openWrite()``/``openRead()`` may retry up to six
times; ``write()``/``read()``/``sync()``/``close()`` can each wait tens of
seconds; ``format()`` is destructive and can take minutes (per-wait cap 120 s).
``bitbang*()``/``showNeoPixels()``/``savePINMode()``/``diagnostics()`` only do a
bounded number of I²C transactions.


Non-blocking API
----------------

For sketches that cannot block, the submit/poll primitives never sleep:
``beginOpenWrite()``, ``beginOpenRead()``, ``beginClose()``, ``beginFormat()``
and ``beginQuery()`` submit a command and return a :cpp:struct:`MicroSDExpOp`
token; ``pollOp()`` does one STATUS read and returns
``MICROSDEXP_ASYNC_BUSY``, ``MICROSDEXP_ASYNC_OK`` or a module error.
``writeNow()``/``readNow()`` move only the bytes that fit right now (zero means
"try again later"), and ``committedBytes()`` reports the commit counter.

Only one async operation may be pending at a time. The API is compiled out by
default in ``I2CSDMODULE_MINIMAL`` and ``Tinyi2cStorage`` builds; override with
``I2CMICROSD_NO_ASYNC=0/1``.

.. literalinclude:: ../examples/AsyncWrite/AsyncWrite.ino
    :caption: examples/AsyncWrite/AsyncWrite.ino
    :language: cpp
    :linenos:

Checked queries
---------------

The checked overloads :cpp:func:`I2CmicroSD::exists`,
:cpp:func:`I2CmicroSD::fileSize` and :cpp:func:`I2CmicroSD::isDirectory` return
``true`` only when the command succeeded and write the answer through the
``out`` pointer, so "not found" is distinct from an I²C/card error.
:cpp:func:`I2CmicroSD::lastCallOk` is shorthand for checking ``lastError()``.
The original one-argument forms are unchanged.

