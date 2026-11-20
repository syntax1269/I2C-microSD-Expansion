Examples
########

Every example is a standalone sketch. Open it from ``File > Examples >
I2C microSD Expansion`` in the Arduino IDE, or copy the folder into your
sketchbook.

Basic read/write
----------------

.. literalinclude:: ../examples/BasicReadWrite/BasicReadWrite.ino
    :caption: examples/BasicReadWrite/BasicReadWrite.ino
    :language: cpp
    :linenos:

Append and list
---------------

.. literalinclude:: ../examples/FileSizeCheck/FileSizeCheck.ino
    :caption: examples/FileSizeCheck/FileSizeCheck.ino
    :language: cpp
    :linenos:

Long file names
---------------

.. literalinclude:: ../examples/LongFileName/LongFileName.ino
    :caption: examples/LongFileName/LongFileName.ino
    :language: cpp
    :linenos:

Directory and card queries
--------------------------

.. literalinclude:: ../examples/BitBangScript/BitBangScript.ino
    :caption: examples/BitBangScript/BitBangScript.ino
    :language: cpp
    :linenos:

NeoPixel strip
--------------

.. literalinclude:: ../examples/NeoPixelBlit/NeoPixelBlit.ino
    :caption: examples/NeoPixelBlit/NeoPixelBlit.ino
    :language: cpp
    :linenos:

Timed pin scripts
-----------------

.. literalinclude:: ../examples/AbruptRecovery/AbruptRecovery.ino
    :caption: examples/AbruptRecovery/AbruptRecovery.ino
    :language: cpp
    :linenos:

Nano (minimal build)
--------------------

.. literalinclude:: ../examples/NanoMinimal/NanoMinimal.ino
    :caption: examples/NanoMinimal/NanoMinimal.ino
    :language: cpp
    :linenos:

ATtiny85
--------

.. literalinclude:: ../examples/Tiny85_16B/Tiny85_16B.ino
    :caption: examples/Tiny85_16B/Tiny85_16B.ino
    :language: cpp
    :linenos:

Web explorer (ESP32)
--------------------

.. literalinclude:: ../examples/WebSDModule/WebSDModule.ino
    :caption: examples/WebSDModule/WebSDModule.ino
    :language: cpp
    :linenos:

Web explorer (ESP32, AsyncWebServer)
------------------------------------

.. literalinclude:: ../examples/WebSDModuleAsync/WebSDModuleAsync.ino
    :caption: examples/WebSDModuleAsync/WebSDModuleAsync.ino
    :language: cpp
    :linenos:

Web explorer (ESP8266)
----------------------

.. literalinclude:: ../examples/WebSDModuleEsp8266/WebSDModuleEsp8266.ino
    :caption: examples/WebSDModuleEsp8266/WebSDModuleEsp8266.ino
    :language: cpp
    :linenos:

Diagnostics report
------------------

.. literalinclude:: ../examples/Diagnostics/Diagnostics.ino
    :caption: examples/Diagnostics/Diagnostics.ino
    :language: cpp
    :linenos:

Two independent modules
-----------------------

.. literalinclude:: ../tests/TwoInstances/TwoInstances.ino
    :caption: tests/TwoInstances/TwoInstances.ino
    :language: cpp
    :linenos:

Non-blocking write
------------------

.. literalinclude:: ../examples/AsyncWrite/AsyncWrite.ino
    :caption: examples/AsyncWrite/AsyncWrite.ino
    :language: cpp
    :linenos:

Ring-buffer data logger
-----------------------

500 Hz sampling into a 512-byte SRAM ring, drained with ``writeNow()``.

.. literalinclude:: ../examples/SensorRingLogger/SensorRingLogger.ino
    :caption: examples/SensorRingLogger/SensorRingLogger.ino
    :language: cpp
    :linenos:

Persistent config manager
-------------------------

Checked queries, a checksummed config file and a staged (temp + remove +
rewrite) update.

.. literalinclude:: ../examples/ConfigSaveLoad/ConfigSaveLoad.ino
    :caption: examples/ConfigSaveLoad/ConfigSaveLoad.ino
    :language: cpp
    :linenos:

Dual-module mirror / failover
-----------------------------

Two modules on one bus, with ``stickyError()`` failover.

.. literalinclude:: ../examples/DualModuleMirror/DualModuleMirror.ino
    :caption: examples/DualModuleMirror/DualModuleMirror.ino
    :language: cpp
    :linenos:

Sensor alert with module-side strobe
------------------------------------

CSV logging plus a timed bit-bang script that the module plays on PB2.

.. literalinclude:: ../examples/ThresholdBitbangAlert/ThresholdBitbangAlert.ino
    :caption: examples/ThresholdBitbangAlert/ThresholdBitbangAlert.ino
    :language: cpp
    :linenos:
