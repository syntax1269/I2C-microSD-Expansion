API Reference
#############

The public interface is declared in ``src/I2C_microSD_Expansion.h``. Include it
as ``#include <I2C_microSD_Expansion.h>``.

Main class
----------

.. cpp:class:: I2CmicroSD

   One instance per module on the I²C bus. The sketch owns and configures the
   ``Wire`` bus before calling :cpp:func:`I2CmicroSD::begin`.

   .. cpp:function:: I2CmicroSD()

   .. cpp:function:: bool begin(uint8_t address = I2CMICROSD_DEFAULT_ADDRESS, uint32_t clockHz = 400000)

      Probe the module at ``address`` (default ``0x6E``) and clock speed.
      Returns ``true`` when the module answers STATUS.

   .. cpp:function:: uint8_t address() const

      Address actually bound by :cpp:func:`I2CmicroSD::begin`, including any
      fallback.

   .. cpp:function:: void setPollInterval(uint16_t ms)

      Set the STATUS poll pause. Lower values trade bus traffic for latency.

   .. cpp:function:: void setReadChunkBytes(size_t n)
   .. cpp:function:: size_t readChunkBytes() const

      Bytes requested per READ drain, clamped to ``1..I2CMICROSD_READ_CHUNK_MAX``.
      Raise to 256 only after enlarging the host ``Wire`` receive buffer
      (for example ``Wire.setBufferSize(256)`` before ``Wire.begin()``) and
      only when the caller's buffer holds a full chunk.

Low-level transport (advanced)
------------------------------

.. cpp:function:: void I2CmicroSD::recoverBus()

   Best-effort recovery for a stuck bus: nine general-call writes to address
   ``0x00``. Not called by :cpp:func:`I2CmicroSD::begin`; call only when you
   know the bus is stuck. Cores that reject ``0x00`` simply report errors.

.. cpp:function:: bool I2CmicroSD::readStatus(StatusFrame &out)
.. cpp:function:: int I2CmicroSD::waitForOpComplete(uint8_t doneBefore, uint32_t timeoutMs = I2CMICROSD_OP_TIMEOUT_MS)
.. cpp:function:: bool I2CmicroSD::waitForCommitted(uint32_t target, uint32_t timeoutMs = I2CMICROSD_COMMIT_TIMEOUT_MS)

File API
--------

.. cpp:function:: bool I2CmicroSD::openWrite(const char *path)
.. cpp:function:: bool I2CmicroSD::openRead(const char *path)
.. cpp:function:: bool I2CmicroSD::openAppend(const char *path)

   Create/truncate, open for reading, or open for append. Append commits are
   atomic per chunk.

.. cpp:function:: size_t I2CmicroSD::write(const uint8_t *buf, size_t len)
.. cpp:function:: size_t I2CmicroSD::read(uint8_t *buf, size_t len)

   Flow-paced transfers. ``read`` returns ``0`` at EOF.

.. cpp:function:: bool I2CmicroSD::sync()

   Wait until all writes are committed to the card.

.. cpp:function:: bool I2CmicroSD::close()

   Close the open file.

.. cpp:function:: bool I2CmicroSD::fileOpen() const

Directories and queries
-----------------------

.. cpp:function:: bool I2CmicroSD::mkdir(const char *path)
.. cpp:function:: bool I2CmicroSD::rmdir(const char *path)
.. cpp:function:: bool I2CmicroSD::remove(const char *path)

   Create a directory, remove an empty directory, or delete a file. Not
   available in ``I2CSDMODULE_MINIMAL`` builds.

.. cpp:function:: bool I2CmicroSD::exists(const char *path)
.. cpp:function:: uint32_t I2CmicroSD::fileSize(const char *path)
.. cpp:function:: bool I2CmicroSD::isDirectory(const char *path)

.. cpp:function:: uint8_t I2CmicroSD::listDir(const char *path, DirEntry *out, uint8_t outCap)

   Fill ``out`` with up to ``outCap`` entries. A return value equal to
   ``outCap`` means **possibly truncated**, not an exact fit: raise the cap
   or page through the directory when the distinction matters.

Card and module information
---------------------------

.. cpp:function:: uint8_t I2CmicroSD::version(char *buf, uint8_t buflen)

   Read the firmware version string. Returns ``0`` with ``MICROSDEXP_INVALID``
   when ``buflen`` is too small.

.. cpp:function:: uint8_t I2CmicroSD::firmwareBanner(char *buf, uint8_t buflen)
.. cpp:function:: uint32_t I2CmicroSD::firmwareClockHz()

   Full CMD_VERSION answer and measured module clock. The banner is available
   on firmware 18.8.0 and newer; ``firmwareClockHz()`` returns ``0`` on older
   firmware.

.. cpp:function:: bool I2CmicroSD::cardInfo(CardInfo *out)

   Card/volume snapshot (mounted state, FAT type, sector count, card SPI
   clock). ``CardInfo`` is an alias of ``CardInfoFrame``.

.. cpp:function:: uint16_t I2CmicroSD::temperatureK()
.. cpp:function:: uint16_t I2CmicroSD::adcPA5()

   Whole kelvin and raw 12-bit ADC. Both return ``0xFFFF`` on any failure.

.. cpp:function:: void I2CmicroSD::setClockTime(uint8_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
.. cpp:function:: void I2CmicroSD::reboot()
.. cpp:function:: bool I2CmicroSD::format(uint8_t mode = 0)

   ``format()`` is destructive: ``0`` is automatic, ``1`` forces an FAT rebuild.

Expansion pins and bit-bang
---------------------------

.. cpp:function:: bool I2CmicroSD::setPinMode(uint8_t pin, uint8_t mode)
.. cpp:function:: bool I2CmicroSD::writePin(uint8_t pin, uint8_t value)

   Mode ``0`` is digital output; a nonzero ``writePin`` value means high.
   Redundant calls with the same mode are a library-side no-op.

.. cpp:function:: void I2CmicroSD::savePINMode()

   Persist pin modes and I²C address to module EEPROM. Explicit-only; a
   redundant call is a no-op if nothing changed.

.. cpp:function:: bool I2CmicroSD::bitbangSetMode(uint8_t pin)
.. cpp:function:: bool I2CmicroSD::bitbangBuffer(uint16_t offset, const uint8_t *data, uint8_t len)
.. cpp:function:: bool I2CmicroSD::bitbangExecute(uint8_t profile)
.. cpp:function:: void I2CmicroSD::bitbangClear()

   The module-side bit-bang workspace is 256 bytes; bytes addressed past it
   are dropped, so a NeoPixel frame is at most 255 bytes (85 pixels).

.. cpp:function:: bool I2CmicroSD::showNeoPixels(uint8_t pin, const uint8_t *grb, uint16_t pixelCount)

   Push a whole GRB frame and latch it in one call. GRB, three bytes per pixel,
   green first.

Error handling
--------------

.. cpp:function:: MicroSDExpError I2CmicroSD::lastError() const

   Result of the most recent call. Internal calls can overwrite it.

.. cpp:function:: MicroSDExpError I2CmicroSD::stickyError() const
.. cpp:function:: void I2CmicroSD::clearError()

   ``stickyError()`` is the first failure since ``clearError()`` and cannot be
   hidden by internal traffic.

.. cpp:enum:: MicroSDExpError

   .. cpp:enumerator:: MICROSDEXP_OK = 0

      No error.

   .. cpp:enumerator:: MICROSDEXP_NO_MODULE

      Module not responding on I²C.

   .. cpp:enumerator:: MICROSDEXP_NO_FILE

      No file open for read/write.

   .. cpp:enumerator:: MICROSDEXP_OPEN_FAIL

      File open was rejected.

   .. cpp:enumerator:: MICROSDEXP_CARD_ERR

      SD card or filesystem error.

   .. cpp:enumerator:: MICROSDEXP_QUEUE_FULL

      Module request queue full.

   .. cpp:enumerator:: MICROSDEXP_INVALID

      Invalid argument, such as a bad path or too-small version buffer.

   .. cpp:enumerator:: MICROSDEXP_OVERFLOW

      Module RX buffers full.

   .. cpp:enumerator:: MICROSDEXP_TIMEOUT

      Operation timed out.

C-style file handle
-------------------

.. cpp:struct:: MicroSDExpFile

   Returned by :cpp:func:`openFile`. One file can be open at a time; the struct
   exposes its owner as ``microSDexp``.

.. cpp:function:: MicroSDExpFile *openFile(I2CmicroSD *b, const char *path, const char *mode)

   ``mode`` is ``"r"``, ``"w"`` or ``"a"``.

.. cpp:function:: size_t fileWrite(MicroSDExpFile *f, const uint8_t *buf, size_t len)
.. cpp:function:: size_t fileRead(MicroSDExpFile *f, uint8_t *buf, size_t len)
.. cpp:function:: bool fileSync(MicroSDExpFile *f)
.. cpp:function:: bool fileClose(MicroSDExpFile *f)

Build flavors
-------------

* Default build: full API.
* ``I2CSDMODULE_MINIMAL``: keeps only ``begin``, the file core and directory
  queries with a small RAM footprint.
* ``Tinyi2cStorage``: uses ``TinyWireM`` instead of ``Wire`` for ATtiny85.
