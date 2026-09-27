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

Optional features (`FEATURES`, each becomes `-DFEATURE_<NAME>`):

- `FACTORY_TEST`: holding the button at power-up enters the factory test
- `WALLSWITCH`: PB4 three-way wall-switch input (unpopulated on this model)
- `TELEMETRY`: PIR samples as `<value>,<motion*100>` on USART2 (PA2, 115200 8N1)
- `SENSOR_TEST`: DP 115 sensor test modes and DPs 125/126

## First flash

1. **Run `make backup` first.** It saves the bootloader, the stock
   application and the device's settings.
2. Check that OpenOCD sees 512-byte pages (`flash info 0`). The `stm32f0x`
   driver may assume 1 KB pages on this part, which would erase the wrong
   ranges. If it does, flash with pyOCD and the pack's `FT32F0xx_64.FLM`.
3. `make flash` writes only the application area. The bootloader
   (`0x08000000`) and settings (`0x0800F000`) are left alone.

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

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware; one module per spec section (see headers) |
| `startup/`, `linker/` | GCC startup (vector relocation to SRAM) and linker script |
| `test/` | Host unit tests |
| `tools/` | `mkimage.py` (OTA header), `gen_tables.py` |
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
