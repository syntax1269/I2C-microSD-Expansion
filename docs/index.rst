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
