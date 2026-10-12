# pir-switch

Replacement firmware for an FT32F030C8AT7 Tuya Wi-Fi smart switch (TRIAC
soft-start, HS6605 PIR, sound and light sensing), written from
[`firmware_spec.md`](firmware_spec.md).

The image runs behind the stock bootloader at `0x08001000` and keeps its OTA
contract (staging at `0x08008000`, install marker at `0x0800EFFC`). The
settings format is unchanged, so an existing device keeps its settings and
pairing, and the Tuya product key and DPs are unchanged.

## Build

Requires `arm-none-eabi-gcc`, `make` and `python3`.

| Command | Result |
|---|---|
| `make` | `build/pir-switch.{elf,bin,hex}`; the `.bin`/`.hex` carry the patched OTA header |
| `make test` | Host unit tests of the hardware-independent modules |
| `make FEATURES="..."` | Choose optional features (default: all) |
| `make tables` | Regenerate `src/tables.c` (sound/light lookup tables) |
| `make backup` | Dump the full 64 KB flash with OpenOCD |
| `make flash` | Program `build/pir-switch.hex` with OpenOCD |
| `make ota IP=…` | Update the MCU over Wi-Fi through OpenBeken (needs the patches in `openbeken/patches/`) |

Optional features (`FEATURES`, each becomes `-DFEATURE_<NAME>`):

- `FACTORY_TEST`: holding the button at power-up enters the factory test
- `WALLSWITCH`: PB4 three-way wall-switch input (unpopulated on this model)
- `TELEMETRY`: PIR samples as `<value>,<motion*100>` on USART2 (PA2, 115200 8N1)
- `SENSOR_TEST`: DP 115 sensor test modes and DPs 125/126

## First flash

1. **Run `make backup` first.** It saves the bootloader, the stock
   application and the device's settings.
2. `make flash` erases and programs only the application area, verifies it,
   and leaves the bootloader (`0x08000000`) and settings (`0x0800F000`)
   alone, then resets into the new firmware. It uses
   the FT32 flash commands in `openocd/ft32f0x.cfg`, not OpenOCD's STM32
   flash driver, which would use the wrong page size on this part.

### OTA

The stock firmware installs an OTA image only if the 16 bytes at image offset
`0x1000` match its product key. This image carries the key there
(`0x08002000`), so the stock firmware should also accept it over Tuya OTA.
That path is untested, so installing over SWD is the safe first step.

This firmware checks more before it writes the install marker:
- the image header at offset `0xC0` (magic `PIRS`, length, CRC-32);
- the product key at `0x1000`.

A stock image has no `PIRS` header, so this firmware won't OTA back to stock.
Use SWD (and `make backup`) to go back.

Mains is present on the board: use an isolation transformer when probing.

## Flashing the Wi-Fi module (OpenBeken) over the diag UART

`bridge/bridge.c` is a small helper that runs from SRAM and forwards the
Beken module's UART (USART1, PA9/PA10) to the diag UART (USART2, PA2/PA3),
both at 115200 8N1. You don't have to solder to the module's pins. It never
writes flash; reset or power-cycle the device to get the installed firmware
back.

1. Connect a 3.3 V USB-serial adapter to the diag UART: adapter RX to PA2,
   adapter TX to PA3, and GND. Keep SWD connected.
2. `make bridge-run`. This stops the MCU at its reset vector, loads the
   bridge into SRAM, verifies it and starts it. The green LED shows it is
   running.
3. Start the Beken flasher on the adapter's serial port at **115200 baud**.
   The bridge doesn't follow baud-rate changes, so if the flasher offers a
   faster transfer rate, keep it at 115200.
4. When the flasher is waiting for the chip, press the device's button. This
   pulses the module reset line (PB10) low for 100 ms (the red LED lights) and
   restarts the Beken into its boot ROM. The white LEDs flicker with traffic.

Mains is present on the board: power it from an isolated supply or an
isolation transformer while it is open.

## Debugging the FT32 with OpenOCD

Use `openocd/ft32f0x.cfg` instead of `target/stm32f0x.cfg`:

    openocd -f interface/cmsis-dap.cfg -f openocd/ft32f0x.cfg

If the debug port stops responding ("cannot read IDR") or returns a garbled
DPIDR, check the ground between the probe and the board first: a weak ground
caused outages of up to a minute here. `make SWD_KHZ=100 ...` lowers the SWD
clock. The config also works around the chip's own quirks:

- **The watchdog keeps running while the core is halted.** The freeze
  register only accepts writes while the core is halted, and the RM says
  DBG_IWDG_STOP enables a hardware watchdog on this part, so it is never
  set. The config refreshes the IWDG on every halt and during flash
  operations. After `reset halt` the IWDG isn't running at all, which is why
  the Makefile targets start that way.
- **No flash bank is declared**, because OpenOCD's STM32 driver would use the
  wrong page size. Use `ft32_flash_write_bin <file.bin> <address>` and
  `ft32_flash_erase <start> <end>` instead (512-byte pages, 32-bit words).
- **The DBGMCU clock is enabled**, so DBGMCU_IDCODE reads correctly.

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware; one module per spec section (see headers) |
| `bridge/` | Wi-Fi UART bridge for flashing the Beken module (runs from SRAM) |
| `openbeken/` | OpenBeken template and `autoexec.bat` for the Beken module after reflashing |
| `startup/`, `linker/` | GCC startup (vector relocation to SRAM) and linker script |
| `test/` | Host unit tests |
| `tools/` | `mkimage.py` (OTA header), `gen_tables.py`, `sched.py` (encode/decode schedule DPs 116-122) |
| `vendor/FT32F0xx_DFP/` | Unmodified FMD device pack: CMSIS headers, drivers, SVD |

## Provenance of the less obvious parts

- The PIR detector (`src/pir_detect.c`) is a port of the stock routine at
  `0x08003CD0`. Its motion output and internal state were checked against the
  stock machine code running under an emulator: identical over 400k samples.
- The wall-switch logic (`wallswitch_poll` in `src/app.c`) is hand-ported
  from `0x08005EEC` and has not been emulator-checked.

## Not yet verified on hardware

- **PIR release:** as in the stock firmware, motion is released only after
  125 consecutive samples with |v| < 50. That needs an idle signal close to
  zero. The spec's capture (idle about -60 +- 20) was taken right after
  power-on with the sensor held in the hand, so it doesn't show the settled
  idle level. Confirm with the telemetry stream in an empty room.
- The HS6605 slot timings in `src/pir.c` are cycle counts; check them against
  the spec's 13.25 us slot with a logic analyser.
