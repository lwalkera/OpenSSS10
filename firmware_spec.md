# Firmware Reverse-Engineering Specification
## FT32F030C8AT7 (STM32F030-compatible) Tuya Wi-Fi Smart Switch with Soft-Start TRIAC, PIR, Sound and Light Sensing

Source: 64 KB flash dump (`dir.bin`) plus bench observations and a PB12 logic capture.
Purpose: provide enough detail to rewrite the application firmware in C with equivalent behaviour.

**Confidence tags used throughout**

| Tag | Meaning |
|---|---|
| **[C]** | Confirmed: read directly from code/constants and/or verified on hardware |
| **[H]** | High: read from code, not yet verified on hardware |
| **[M]** | Medium: interpretation of code whose purpose is inferred |
| **[?]** | Unknown / not yet decoded |

Addresses of the original routines are given as `@0x0800xxxx` so the disassembly can be consulted when a detail matters.

---

## 1. Product summary

A mains wall/plug-in device that switches a lamp through a TRIAC with optional soft fade, controlled by:

- a Tuya Wi-Fi module on USART1 (Tuya MCU serial protocol, 9600 baud),
- a push button (PF7),
- a PIR motion sensor via an HS6605 digital PIR front-end (single-wire readout on PB12),
- a microphone (PA1) and a light sensor (PA6),
- seven weekly schedule slots, a countdown timer, and a built-in default "PIR night-light" schedule.

It also has a built-in LED night-light/indicator: 3 white LEDs (one PWM group) and 3 RGB LEDs (cyan in normal use), dimmed according to ambient light.

---

## 2. MCU, clocks, memory

### 2.1 MCU
FMD **FT32F030C8AT7** (LQFP48): an STM32F030-register-compatible Cortex-M0 (ARMv6-M, Thumb) with 64 KB flash organised as **128 pages of 512 bytes**, and 8 KB SRAM. **[C]** Sources: FMD FT32F03xx6/x8 datasheet v1.14 and FT32F0xxx6/x8 reference manual v1.50. Note that the FMD FT32**E**030 family is a different design and is not register-compatible.

### 2.2 Clock tree (original)
`SystemInit @0x08002010` **[C]**:
- HSI on, wait HSIRDY.
- FLASH_ACR = `0x11` (prefetch enable, 1 wait state).
- RCC_CFGR: clear bits 15–21, then set `PLLSRC[1:0]=01` (bit 15, "HSI/PREDIV") and `PLLMUL=0100` (×6); PREDIV = /1.
- PLLON, wait PLLRDY, SW = PLL, wait SWS = PLL.
- Result: **SYSCLK = HCLK = PCLK = 48 MHz**.

This is a documented setting on the FT32F0xx: RCC_CFGR `PLLSRC[1:0]` = 00 HSI/2, **01 HSI/PREDIV**, 10 HSE/PREDIV, 11 HSI48/PREDIV. Observed behaviour matches 48 MHz (1.5 Hz pairing blink, ~3 min pairing timeout, working 9600-baud Tuya link). If the code is ever ported to a genuine STM32F030x8, where bit 15 is reserved, use HSI/2 × 12 instead.

### 2.3 Flash map
| Range | Size | Content |
|---|---|---|
| 0x0800_0000–0x0800_07FF | 2 KB | Bootloader (§3) |
| 0x0800_0800–0x0800_0FFF | 2 KB | Unused (erased) |
| 0x0800_1000–0x0800_7FFF | 28 KB | Application image (≈24 KB used; vector table at 0x0800_1000) |
| 0x0800_8000–0x0800_EFFF | 28 KB | OTA staging image; last word 0x0800_EFFC = install marker |
| 0x0800_F000–0x0800_FFFF | 4 KB | Settings storage (§13) |

### 2.4 RAM
- Initial SP (application) = `0x2000_1418` (~5 KB used incl. stack).
- 0x2000_0000–0x2000_00BF: vector table copy (see §2.5).

### 2.5 Vector-table relocation
Cortex-M0 has no VTOR. At start of `main @0x08005DB4` the application **[C]**:
1. disables interrupts,
2. copies 64 words from 0x0800_1000 to 0x2000_0000,
3. sets SYSCFG_CFGR1.MEM_MODE = 3 (SRAM at 0x0000_0000).

Interrupts used (all others default handler):

| IRQ | Handler | Purpose |
|---|---|---|
| EXTI4_15 (IRQ 7) | `@0x08001A90` | PB5 zero-cross edge (§9.1) |
| TIM14 (IRQ 19) | `@0x08001F48` | TRIAC gate timing (§9.2) |

SysTick and USART interrupts are **not** used. Everything else is polled from the main loop.

---

## 3. Bootloader (0x0800_0000) **[C]**

No UART; runs from reset, SP = 0x2000_0600.

1. Read word at 0x0800_8000 (staging image SP). If `(SP & 0x2FFE0000) == 0x20000000` **and** word at 0x0800_EFFC == `0x4E454C45` (bytes `E L E N`, reads "NELE" in a hex dump):
   - erase app area 0x0800_1000–0x0800_7FFF (56 pages of 512 B),
   - copy 0x7000 bytes from 0x0800_8000 to 0x0800_1000 via a 128-byte RAM buffer,
   - overwrite 0x0800_EFFC with 0 (clears marker).
2. Disable interrupts, set MSP = [0x0800_1000], jump to [0x0800_1004].

The application's OTA code (§8.6) writes the staging image and the marker, then resets.

---

## 4. Pinout **[C]**

| Pin | Mode | Function |
|---|---|---|
| PA1 | Analog | ADC_IN1 – microphone amplifier output |
| PA2 | AF1 | USART2_TX – debug/telemetry, 115200 8N1 |
| PA3 | AF1 | USART2_RX – DMA'd but never read |
| PA6 | Analog | ADC_IN6 – light sensor |
| PA7 | AF1 | TIM3_CH2 – white LED PWM (inverted polarity) |
| PA9 | AF1 | USART1_TX → Tuya module RX |
| PA10 | AF1 | USART1_RX ← Tuya module TX |
| PA13/PA14 | — | SWD (untouched) |
| PB0 | AF1 | TIM3_CH3 – white LED PWM (inverted) |
| PB1 | AF1 | TIM3_CH4 – white LED PWM (inverted) |
| PB3 | Output PP, high speed, pull-up, idle **high** | TRIAC gate (opto LED), **active low** |
| PB4 | Input, pull-up | Wall-switch mains sense (three-way variant only; unpopulated on this model) |
| PB5 | Input, pull-up, EXTI5 rising | Mains zero-cross (optocoupler), one rising edge per mains cycle |
| PB10 | Output PP, idle **high** | Tuya module reset, pulsed low 100 ms |
| PB12 | Input, no pull; driven by bit-bang | HS6605 pin 8 single-wire data (§11.1) |
| PB13 | AF2 | TIM1_CH1N – RGB green |
| PB14 | AF2 | TIM1_CH2N – RGB blue |
| PB15 | AF2 | TIM1_CH3N – RGB red |
| PF7 | Input, pull-up | Push button, active low |

All output/AF pins are configured with high speed; UART pins push-pull, no pull.

---

## 5. Peripheral configuration (48 MHz)

### 5.1 TIM6 – millisecond timebase **[C]**
PSC = 47999 (1 kHz), ARR = 0xFFFF, free running, no interrupt. The main loop reads CNT and accumulates elapsed milliseconds into a `{uint32 seconds; uint16 ms}` clock (§6).

### 5.2 TIM1 / TIM3 – LED PWM **[C]**
- PSC = 0, ARR = 999 → **48 kHz**, CCR range 0–1000 (1000 = 100 %).
- PWM mode 1 (OCxM = 110), preload off.
- TIM3: CH2/3/4 enabled with **CCxP = 1** (inverted). CCER = 0x3330.
- TIM1: CH1N/2N/3N enabled (CCxNE, active high). CCER = 0x0444. BDTR.MOE = 1.
- The TIM3 channels are always written with the same value.

### 5.3 TIM14 – TRIAC timing **[C]**
PSC = 479 → **10 µs tick**, ARR = 0xFFFF. CC1 in output-compare (frozen) mode, DIER.CC1IE = 1. NVIC enabled. CNT is read and reset by the zero-cross ISR.

### 5.4 TIM15 – ADC trigger **[C]**
PSC = 47, ARR = 124 → 8 kHz update; CR2.MMS = update (TRGO). Started/stopped by the ADC state machine.

### 5.5 ADC + DMA1 channel 1 **[C]**
- ADC_CFGR1 = 0x0001050B: 10-bit, right aligned, EXTSEL = TIM15_TRGO, EXTEN = rising, DISCEN, DMAEN, DMACFG = circular.
- ADC_CCR.VREFEN = 1 (not otherwise used).
- SMPR = 0 (1.5 cycles).
- Calibration run before enabling.
- DMA1_CH1: periph = ADC_DR, mem = 512 × uint16 buffer, MINC, CIRC, 16-bit/16-bit, CNDTR = 512.

### 5.6 USART1 / USART2 **[C]**
| | USART1 | USART2 |
|---|---|---|
| BRR | 0x1388 (9600) | 0x01A1 (115200) |
| Frame | 8N1 | 8N1 |
| CR3 | DMAR | DMAR |
| RX DMA | DMA1_CH3, 512 B circular | DMA1_CH5, 512 B circular |
| TX | polled, TXE | polled, TXE |

DMA CCR = 0x20A1 (enable, circular, MINC, priority high, periph→mem, 8-bit).

### 5.7 IWDG **[C]**
PR = 6 (/256), RLR = 0x618 → ≈10 s with the 40 kHz LSI. Refreshed every main-loop pass. LSI enabled via RCC_CSR before start.

### 5.8 EXTI **[C]**
SYSCFG_EXTICR2: EXTI5 = port B. IMR bit 5, RTSR bit 5 (rising). NVIC EXTI4_15 enabled.

---

## 6. Software architecture

### 6.1 Time base and software timers **[C]**
Global clock `now = {uint32 sec; uint16 ms}` advanced from TIM6 CNT deltas each loop (`@0x080041B4`).

Timer object = `{uint32 sec; uint16 ms}` holding an absolute expiry time:
- `timer_set_ms(t, ms)` `@0x08003F88`: expiry = now + ms (ms < 5000 handled exactly; larger values rounded to whole seconds).
- `timer_set_s(t, s)` `@0x08003FF8`: expiry = now + s seconds.
- `timer_expired(t)` `@0x0800414C`: true if now ≥ expiry.
- `delay_ms(ms)` `@0x08004014`: blocking busy-wait using TIM6.

### 6.2 Initialisation order (`main @0x08005DB4`)
1. Vector table to SRAM, SYSCFG remap.
2. Storage slot table init.
3. TIM6 timebase.
4. UART driver (USART1/2, DMA RX, port structures).
5. LED PWM (TIM1/TIM3), LED state.
6. PF7 button GPIO.
7. ADC/TIM15/DMA sampler.
8. Light-sensor module (callback registration).
9. Sound module (callback registration).
10. HS6605 PB12 GPIO + PIR state.
11. TRIAC: PB3 gate, PB5 EXTI, TIM14.
12. PB4 wall-switch input.
13. Tuya protocol layer (PB10 module reset pin output high).
14. Local time module.
15. Application defaults.
16. OTA state.
17. Register Tuya callbacks (§8.3).
18. Enable interrupts.
19. Load settings/schedules from flash.
20. Start IWDG.

### 6.3 Main loop (in order)
```c
for (;;) {
    iwdg_refresh();
    clock_update();              // TIM6 -> now
    uart_poll();                 // DMA RX idle detect, TX pump (both ports)
    led_effects_poll();          // fades, blink, breathing, apply PWM
    button_poll();               // PF7 debounce
    adc_sampler_poll();          // sound/light bursts, callbacks
    pir_poll();                  // HS6605 read + motion detection + debug print
    triac_poll();                // fade stepping, gate table computation
    wallswitch_poll();           // PB4
    tuya_poll();                 // RX parser, TX, command dispatch
    time_poll();                 // periodic 0x1C local-time requests
    app_poll();                  // load/LED/schedule/pairing/report state machines
    ota_poll();                  // post-download reset
}
```

---

## 7. UART driver layer (`@0x08006A34`, `@0x08006C74`) **[C]**

Two port objects (index 0 = USART1/Tuya, index 1 = USART2/debug), 0x24 bytes each:

```c
typedef struct {
    USART_TypeDef       *usart;
    DMA_Channel_TypeDef *rx_dma;
    sw_timer_t           rx_idle;     // restarted 5 ms after each new byte
    uint8_t             *rx_buf;      // 512 B, DMA circular
    uint8_t             *tx_ring;     // 4 B
    uint16_t rx_size;                 // 512
    uint16_t rx_rd;                   // consumer index
    uint16_t rx_wr;                   // = 512 - CNDTR
    uint16_t tx_size;                 // 4
    uint16_t tx_rd, tx_wr;
} uart_port_t;
```

Per loop, for each port:
1. `wr = rx_size - DMA->CNDTR`; if it changed, store it and restart `rx_idle` (5 ms).
2. If `tx_rd != tx_wr` and `ISR.TXE`, write one byte to TDR and advance `tx_rd`.

API: `avail()`, `getc()`, `read(n)`, `flush()` (rd = wr), `putc()` (returns 0 if the ring is full), `tx_has_space()`.

USART2 debug output bypasses the ring and busy-waits on TXE (§11.4).

---

## 8. Tuya MCU serial protocol (USART1, 9600 8N1)

### 8.1 Frame **[C]**
```
55 AA | ver | cmd | len_hi len_lo | data[len] | sum
```
- `sum` = (sum of all preceding bytes) & 0xFF.
- MCU always transmits `ver = 0x03`; received version is ignored.
- Maximum accepted frame: 267 bytes (`len` ≤ 260).
- TX frame is assembled in a buffer and pumped one byte per loop pass through the 4-byte ring.

### 8.2 Receive parser (`@0x080066B8`) **[H]**
1. Wait for any received byte, then start a 1000 ms frame timer.
2. Need 2 bytes: must be `55 AA`, otherwise flush and resync.
3. Need 4 more bytes: `ver cmd len_hi len_lo`; total = len + 7; if > 267, resync.
4. Collect bytes until `total`.
5. When the TX frame buffer is empty, verify the checksum, then dispatch on `cmd`.
6. Timeout of the 1000 ms timer at any stage → flush and resync.

### 8.3 Commands handled from module **[C]**
| cmd | Meaning | MCU action / reply |
|---|---|---|
| 0x00 | Heartbeat | Reply `00` + 1 byte = the "module alive" flag before this heartbeat (0x00 on the first heartbeat after MCU boot or after an MCU-initiated module reset, 0x01 afterwards). Sets "module alive" flag and restarts a 180 s watchdog; if it expires, the module is reset via PB10 (low 100 ms). |
| 0x01 | Product info | Reply with `{"p":"urlxtrtlq9fjkv2d","v":"2.3.4","m":2,"mt":3,"n":0,"low":0}` (63 bytes, no terminator) |
| 0x02 | Working mode | Empty reply (module handles its own LED/button) |
| 0x03 | Wi-Fi status (1 byte) | Callback `wifi_status(s)` (§8.5), empty reply |
| 0x04 | — | Ignored |
| 0x05, 0x0C, 0x0E, 0x1C, 0x2B | Responses to MCU requests | If `cmd` equals the pending request, mark response received (status 0x10) and clear the pending request |
| 0x06 | DP command | Callback `dp_command(data, len)` (§12.2); state is reported later via 0x07 |
| 0x08 | Query all DPs | Set all 27 report-dirty bits (only if the device is past boot) |
| 0x0A | OTA start (u32 BE size) | Callback `ota_start(size)`; reply `0A` with 1 byte `00` (256-byte packets) |
| 0x0B | OTA data (u32 BE offset + data) | Callback `ota_data(data, len-4, offset)`; empty reply (only if len ≥ 11) |

The five callbacks are registered as a struct at init (`@0x08005E1C`):
```c
struct { void (*query_all)(void); void (*dp_cmd)(uint8_t*,uint16_t);
         void (*wifi_status)(uint8_t); void (*ota_start)(uint32_t);
         void (*ota_data)(uint8_t*,uint16_t,uint32_t); };
```

### 8.4 Commands sent by the MCU **[C]**
| cmd | Data | When |
|---|---|---|
| 0x07 | One or more DP records | DP reporting (§12.3) |
| 0x05 | 1 byte `00` (EZ / smartconfig) | Pairing sequence (§14.3) |
| 0x0E | none | Factory Wi-Fi test; response data[1] = signal strength, pass if ≥ 60 |
| 0x1C | none | Local time request (§15) |

The send helper `@0x0800659C` always arms a pending-response record (expected cmd, 10 s timeout, status 1 = waiting, 0x1F = timed out/idle). Replies to module requests are sent without it.

"Link idle" (`@0x08006640`) = module alive && no unread RX bytes && TX frame buffer empty. All MCU-initiated traffic waits for link idle.

### 8.5 Wi-Fi status callback (`@0x08004A48`) **[H]**
Ignored until boot finishes. The status byte is stored for display logic.
- **4 (connected to cloud):** set connected flag; clear pairing-blink LED bits; if not paired, set paired = 1 and mark settings for saving; set all DP report bits.
- **6:** if paired, clear paired and mark settings for saving; set countdown DP 7 = 0 and report it; restart schedule evaluation; if the pairing state machine is idle (< 0x19), force state 0x19 (LED pairing indication) [M].

### 8.6 OTA (`@0x08003B18`, `@0x08003A04`, `@0x08003B70`) **[C]**
- **Start:** reject if size > 0x6FFC. Erase 0x0800_8000–0x0800_EFFF, set expected = size, received = 0, state = 1, start a 5 s inactivity timer.
- **Data:** accepted only if state = 1 and offset == received, and len % 4 == 0 or it's the last packet. Program 32-bit words at 0x0800_8000 + offset, padding the final partial word with 0xFF. On completion (received == size):
  - compare 16 bytes at 0x0800_9000 (new image +0x1000) with 0x0800_2000 (running image +0x1000). Those 16 bytes are the ASCII product key `urlxtrtlq9fjkv2d`, stored at a fixed location, so this is a product-key match check; any replacement image must carry the same key at image offset 0x1000,
  - if they match, write the marker `0x4E454C45` to 0x0800_EFFC.
- **Poll:** when complete, wait 500 ms, then `NVIC_SystemReset()`; the bootloader installs the image. If no data arrives for 5 s, abort.

**Recommendation:** replace the 16-byte check with a proper header (magic, length, CRC32) in a rewrite.

---

## 9. TRIAC phase control

### 9.1 Zero-cross measurement (EXTI5 ISR `@0x08001A90`) **[C]**
The optocoupler gives **one rising edge per full mains cycle**. On each edge:
```c
uint16_t period = TIM14->CNT;   // 10 µs ticks
TIM14->CNT = 0;
zc_state_fn(period);            // current state handler (function pointer)
```
- **Acquire** (`@0x08006308`): accept only 1428 ≤ period < 2501 (14.3–25 ms, i.e. 40–70 Hz). Accumulate 16 valid periods, average (sum / 16), set "period valid", switch to Run. An invalid period switches to Resync.
- **Resync** (`@0x0800635C`): wait for 10 consecutive valid periods, then return to Acquire.
- **Run** (`@0x08006394`):
  - swap the double-buffered gate table if a new one was computed,
  - load the first gate event into TIM14 CCR1 (or drive the gate immediately if the event time is 0),
  - ignore periods shorter than half the current average (glitch rejection); otherwise accumulate a 30-period average and publish it when it changes.

### 9.2 Gate timing (`@0x08005FF0`, TIM14 ISR `@0x08001F48`) **[C]**
```c
half = period / 2;                  // ticks per half cycle
off30 = half * 30 / 180;            // 30°
if (level == 1000)      { t_on = 0;  t_off = half - off30/2; }
else if (level == 0)    { t_on = half; t_off = 0; }   // never fires
else {
    t_on  = half - (level * (half - off30) / 1000 + off30);
    t_off = half - off30/2;         // release ≈165°
}
table = { t_on, t_off, t_on + half, t_off + half };
```
The TIM14 CC1 ISR walks the four events, alternately driving PB3 low (gate on, via BRR) and high (gate off, via BSRR), and reloads CCR1 with the next event.

The gate is **held on** from the firing angle to about 165°, not pulsed, so the TRIAC stays latched with low-current LED loads. PB3 is active-low.

### 9.3 Fading (`@0x080061C8`, `@0x080061B0`) **[C]**
`triac_set(on, fade_mode)`. `level` (0–1000) is stepped **once per mains cycle**, only after the ISR has consumed the previous table, toward 1000 (on) or 0 (off):

| fade_mode (DP 103 / DP 104) | Step | Full fade at 50 Hz |
|---|---|---|
| 0 | Jump directly to target | Instant |
| 1 | 25 | ≈0.8 s |
| 2 | 3 | ≈6.7 s |
| 3 | 1 | ≈20 s |

States: 0 wait for valid period → 1 idle (fire when on requested) → 2/3 ramping → 4 off-ramp. A "done" flag (`@0x080061A4`) is set when the target is reached; settings saves wait for it.

There is **no intermediate dimming level**: the load is either fading to on or fading to off.

---

## 10. LED subsystem

### 10.1 Outputs **[C]**
| Mask bit | Pins | LED |
|---|---|---|
| 0 | PA7 + PB0 + PB1 (TIM3 CH2/3/4, same CCR) | 3 white LEDs |
| 1 | PB15 (TIM1 CCR3) | RGB red |
| 2 | PB13 (TIM1 CCR1) | RGB green — driven at 1/5 level when mask == 0b0110 |
| 3 | PB14 (TIM1 CCR2) | RGB blue |

Normal cyan = mask `0b1100` (green + blue). Status colour = mask `0b0110` = red at the gamma-corrected level plus green at 1/5 of that duty cycle; it appears **yellow** on this hardware because the green die is much brighter than the red. The colour of each pin was confirmed on hardware: DP 115 = 4 breathes PB13 alone and lights green.

### 10.2 Brightness curve **[C]**
Level 0–100 maps to CCR through the gamma table at 0x0800_6D00 (101 × uint16):
```
0,1,2,3,4,5,6,7,8,9,10,12,14,16,19,22,25,28,32,36,40,44,48,52,57,62,67,72,78,84,89,96,
102,108,115,122,129,136,144,151,159,167,176,184,193,202,211,220,229,239,249,259,269,280,
290,301,312,323,335,347,358,370,383,395,408,421,434,447,460,474,488,502,516,530,545,560,
575,590,605,621,637,653,669,685,702,719,736,753,770,788,806,824,842,860,879,898,917,936,
955,975,1000
```

### 10.3 LED effect engine (`@0x08003260`, `@0x080032B4`, `@0x080033C4`) **[C]**
```c
led_set_target(level);                      // 0..100
led_set(mask, mode, count);                 // mode 0 off, 1 steady, 2 blink, 3 breathe
```
- **Steady (1):** current level ramps by ±1 per 20 ms toward target (smooth fade), applied to `mask`.
- **Blink (2):** 333 ms on / 333 ms off (≈1.5 Hz) at the current level; `count` = number of blinks (0xFF = infinite).
- **Breathe (3):** ramp up 1 step per 20 ms to target, hold 50 × 20 ms (1 s), ramp down to 0, hold 1 s, repeat.
- **Apply:** CCR = gamma[level] on the channels selected by `mask`, 0 on the others.

### 10.4 Output-request word (`@0x08004B60`, 16 bits at 0x2000_090C) **[C]**
Re-rendered when the word, DP 127 or the light level changes by ≥ 10. Checked in priority order:

| Condition | Result |
|---|---|
| word == 0 | All LEDs off |
| bits 8–13 ≠ 0 | **Preview**: level = DP[101/102/105/107] selected by bits 12–13, mask = bits 8–11, steady. Cleared 5 s after the last DP change. Values: DP 101 → 0x0100, DP 102 → 0x1C00, DP 105 → 0x2600. |
| bits 6–7 == 01 | White, blink forever at 100 % (pairing) |
| bits 6–7 == 10/11 | Mask 0b0110 breathe at DP 105 level, or ambient level if DP 127 is on (Wi-Fi reset warning) |
| bits 4–5 == 10 | Steady 100 % on mask = bits 0–3 (factory test) |
| bit 15 | Green (PB13) breathe at 100 % (DP 115 sensor test waiting) |
| otherwise | Steady, mask = bits 0–3, level = DP[101/102/105/107] selected by bits 4–5, or the ambient table if DP 127 is on |

Normal words: **0x01** = white at DP 101 level, **0x1C** = cyan at DP 102 level.

### 10.5 Ambient dimming (DP 127 on) **[C]**
| Light level L | White / other | Cyan (mask 0b1100) |
|---|---|---|
| L < 25 | 20 | 15 |
| 25 ≤ L < 35 | 50 | 50 |
| 35 ≤ L < 50 | 80 | 80 |
| L ≥ 50 | 100 | 100 |

Higher L = brighter room, so the LEDs dim as the room gets darker.

### 10.6 Colour selection (`@0x080049E8`) **[C]**
Low 6 bits of the word, driven by the load state machine (§14.1) and the active schedule record `rec`:

| Load state | No active record | Active record |
|---|---|---|
| 4 (manual-off hold-off) | cyan | cyan |
| 5 (armed / off) | cyan | white if `rec.mode == 2 && dark`, otherwise cyan |
| 7 (on) | cyan | white if `rec.off_mode == 2`, otherwise cyan |

With the default record (§13.3) this gives: **on → white, off (armed, dark) → white, manually switched off → cyan for DP 50 seconds, then white.**

---

## 11. Sensors

### 11.1 HS6605 digital PIR interface (PB12 ↔ HS6605 pin 8) **[C]** (code + logic capture)
The HS6605 (SOIC-8) conditions the pyroelectric sensor and outputs **filtered signed 16-bit samples at ≈63.5 Hz** (period 15.74 ms, varying from 14.7 to 16.8 ms) over a single bidirectional line.

**Line behaviour**
- Idle between frames: **low** (driven by the HS6605).
- **Data ready:** the HS6605 drives the line **high** and keeps it high until it is read.
- PB12 is configured as input with no pull; the MCU switches it to output only during each slot.

**Read procedure** (`@0x0800303C`, bit slot `@0x08003EB8`), with interrupts disabled:
```c
if (!(GPIOB->IDR & (1<<12))) return;        // no sample ready
__disable_irq();
delay_us(~40);
b_hdr1 = read_slot();                       // must be 1
b_hdr0 = read_slot();                       // must be 0
uint16_t v = 0;
for (i = 0; i < 16; i++) v = (v << 1) | read_slot();   // MSB first
b_trl  = read_slot();                       // must be 0
__enable_irq();
valid = (b_hdr1 == 1 && b_hdr0 == 0 && b_trl == 0);
return valid ? (int16_t)v : 0;
```
Measured slot timing (13.25 µs per slot):
```c
int read_slot(void) {
    GPIOB->BRR  = 1<<12;  PB12_mode_output();   // drive low
    delay ≈1.1 µs;
    GPIOB->BSRR = 1<<12;                        // drive high
    delay ≈2.6 µs;
    PB12_mode_input();                          // release; HS6605 drives the bit
    delay ≈9 µs;
    return (GPIOB->IDR >> 12) & 1;              // 1 = stays high, 0 = falls
}
```
Decoding all 410 captured frames gave header `1,0` and trailer `0` every time. The MCU never writes configuration to the HS6605.

**Signal characteristics (capture)**
- Power-up transient up to +18 500 / −5 700, settling in ≈4 s.
- Quiescent baseline ≈ −60, noise ≈ ±20.
- Hand motion: biphasic swings of about +1 600 / −2 600.

### 11.2 Motion detection (`@0x08003C38`) **[M]**
State: 0 → 1 warm-up (discard the first **500 samples**, ≈7.9 s) → 2 run. If no sample arrives for 1 s, go back to state 0.

Parameters come from a per-sensitivity table at 0x0800_6DCA: 3 rows × 37 int16, row = DP 48.
```
row0: 60,0,0,62,0,50,20,0,0,125,0,180,20,4,5,10,0,4,5,10,0,6,10,18,0,4,5,10,0,6,10,18,0,4,8,14,0
row1: identical except T[11] = 110
row2: identical except T[11] = 60
```
Fields used (index into the row):
- **T[11]:** amplitude threshold (180 / 110 / 60).
- **T[13] = 4, T[15] = 10:** event-count thresholds.
- **T[5] = 50, T[9] = 125:** release threshold and number of quiet samples.
- **T[0] = 60, T[3] = 62:** secondary quiet gate.

Algorithm (four rotating lobe windows of 0x1C bytes each):
- While |v| ≥ T[11], accumulate a *lobe* of consistent polarity: sample count, sum of |v|, peak, and number of new peaks.
- **Motion is declared** when the lobe's new-peak count ≥ T[13] and new-peak count plus above-threshold count ≥ T[15].
- A polarity change with |v| ≥ 20 starts a new lobe if the previous lobe had ≥ 30 samples; otherwise all windows reset.
- More than T[11] consecutive sub-threshold samples also resets the windows.
- **Motion is released** after T[9] = 125 consecutive samples (≈2 s) with |v| < T[5] = 50.

Output: `motion` flag (0/1). For an exact reimplementation, follow `@0x08003CD0–0x08003E6A`.

### 11.3 ADC sampler (`@0x080026EC`) **[C]**
A cyclic state machine, polled from the main loop:
1. Select ch1 (PA1), start TIM15 + DMA (512 samples at 8 kHz = 64 ms).
2. When the DMA buffer has wrapped (all 512 samples taken): stop, call `sound_cb(buf, 512)`.
3. Select ch6 (PA6), restart; after ≥ 3 samples, stop and call `light_cb(buf, n)`.
4. Repeat. One cycle ≈ 65 ms, so both sensors update at ≈15 Hz.

### 11.4 Sound level (`sound_cb @0x0800282C`) **[C]**
```c
mean = sum(buf[0..511]) / 512;
avgdev = sum(|buf[i]-mean|) / 512;  maxdev = max(|buf[i]-mean|);
if (avgdev < 20) L = 20 + 38*log10(1 + 0.3*avgdev);
else             L = 57 + 50*log10(0.4 + 0.02*avgdev);
L = min(L, 100);                                   // sound level 0..100
if (threshold == 100) trigger = 0;                 // DP 106 == 100 disables
else trigger = (maxdev > 500) || (L >= threshold);
```
- `threshold` = DP 106, clamped to 20–100 (default 65; 90 in the factory test).
- Peak L over each second is reported as DP 126 in sensor-test modes 3 and 4.

### 11.5 Light level (`light_cb @0x08003478`) **[C]**
```c
x = 1023 - buf[n-1];                              // last sample
L = (x < 301) ? 30*log10(x+1) : 50*log10(x/10.0 + 1);
L = min(L, 100);                                   // higher = brighter
```
Dark flag (hysteresis, `@0x080034DC`): after **5 consecutive** readings with L ≤ `dark_threshold`, set dark = 1; after 5 consecutive readings with L > threshold, set dark = 0.
- `dark_threshold` comes from the active schedule's light preset, which indexes the three configurable thresholds DP 107 / DP 123 / DP 124 (defaults 10 / 20 / 100). A threshold of 100 means the room always counts as dark.
- The factory test checks that L spans more than 80 points.

### 11.6 USART2 telemetry **[C]**
For every valid HS6605 sample (state ≥ 1), print `"<v>,<motion*100>\r\n"` in decimal (leading '-' for negative values), polled TX.

---

## 12. Data points (DPs)

Encoding inside cmd 0x06 / 0x07: `id(1) type(1) len(2, BE) value`. Types: 1 bool (1 B), 2 value (4 B BE, signed), 3 string, 4 enum (1 B).

### 12.1 DP table
"Save" = persisted in the flash settings record. "Bit" = report-dirty bit (§12.3).

| DP | Type | Dir | Meaning | Range / notes | Save | Bit | Conf. |
|---|---|---|---|---|---|---|---|
| 1 | bool | rw | Main switch (TRIAC load) | `switch(v, src=0)`; always off at power-up | – | 0 | C |
| 7 | value | rw | Countdown, seconds | Decremented each second; at 0 → `switch(DP110, src=1)`; reported every 60 s and at 0 | – | 1 | C |
| 48 | enum | rw | PIR sensitivity | 0–2 (≥3 → 2) | ✔ | 2 | C |
| 50 | value | rw | Manual-off hold-off, seconds | PIR ignored after a manual off; LEDs cyan | ✔ | 3 | C |
| 101 | value | rw | White LED level | 0–100; 5 s preview | ✔ | 4 | C |
| 102 | value | rw | Cyan LED level | 0–100; 5 s preview | ✔ | 5 | C |
| 103 | enum | rw | TRIAC fade-in | 0–3 (§9.3) | ✔ | 6 | C |
| 104 | enum | rw | TRIAC fade-out | 0–3 | ✔ | 7 | C |
| 105 | value | rw | Status-colour (red + 1/5 green, appears yellow) level | 0–100; 5 s preview | ✔ | 8 | C |
| 106 | value | rw | Sound threshold | 20–100 (100 = off) | ✔ | 9 | C |
| 107 | value | rw | Dark threshold for light preset 0 | 0–100 (light level at or below = dark) | ✔ | 10 | C |
| 108 | – | – | Not handled | | | | C |
| 109 | string | ro | Hardware/model tag | `"CF.20"` from flash slot 2 (up to 8 chars) | slot 2 | 11 | M |
| 110 | bool | rw | Switch state applied when the countdown ends | | – | 12 | C |
| 111 | value | rw | Unknown | stored in RAM only | – | 13 | ? |
| 112 | – | w | Writing it triggers a report of DP 7 and DP 111 | | – | – | H |
| 113 | string | ro | Firmware version `"2.3.4"` | sent together with DP 109 | – | 11 | C |
| 114 | – | – | Not handled | | | | C |
| 115 | enum | rw | Sensor test mode | §14.4 | – | 14 | C |
| 116–122 | string | rw | Schedule slots 0–6 (base64) | §13.2 | slot 1 | 15–21 | C |
| 123 | value | rw | Dark threshold for light preset 1 | 0–100 | ✔ | 22 | C |
| 124 | value | rw | Dark threshold for light preset 2 (used by the default schedule) | 0–100 | ✔ | 23 | C |
| 125 | value | ro | Light level 0–100 | reported each second in test mode 5 | – | 24 | C |
| 126 | value | ro | Peak sound level of the last second | test modes 3/4 | – | 25 | C |
| 127 | bool | rw | Ambient-adaptive LED brightness | | ✔ | 26 | C |

Defaults (`@0x080041F4`, used on factory reset / first pairing):

| DP | Default |
|---|---|
| 1 | off |
| 7 | 0 |
| 48 | 2 |
| 50 | 10 |
| 101, 102, 105 | 100 |
| 103, 104 | 0 |
| 106 | 65 |
| 107 | 10 |
| 110 | 0 |
| 115 | 0 |
| 123 | 20 |
| 124 | 100 |
| 127 | 1 |
| Schedule slot 0 | the default record (§13.3) |
| Schedule slots 1–6 | disabled |

### 12.2 DP command processing (`@0x08004594`) **[C]**
- Ignored until boot has finished.
- Iterates over all DP records in the payload.
- Persistent DPs set the "settings dirty" flag; schedule DPs set "schedules dirty".
- Every change sets its report-dirty bit, so the new value is echoed back.

### 12.3 DP reporting (`@0x08005830`) **[C]**
A 27-bit dirty mask; `0x07FFFFFF` means report everything (set on cmd 0x08, on cloud connect, and after pairing).

Report pacing:
1. When any bit is set and the link is idle, build **one** 0x07 frame containing the lowest-numbered dirty DP (DP 109 + 113 always go together), clear its bit, and send.
2. Wait for link idle.
3. Wait 150 ms.
4. Repeat.

Schedule strings are only added if they fit in 260 bytes.

---

## 13. Persistent storage and schedules

### 13.1 Flash record format (`@0x08002BBC`–`0x08002D98`) **[C]**
Slot table (start, end):

| Slot | Range | Content |
|---|---|---|
| 0 | 0x0800_F000–0x0800_F200 | settings |
| 1 | 0x0800_F200–0x0800_FA00 | schedules |
| 2 | 0x0800_FE00–0x0801_0000 | DP 109 string |

Record layout:
```
u32 magic = 0x454C454E ("NELE")
u32 count            (number of data words)
u32 data[count]      (every value, even bytes/chars, stored as a full word)
u32 checksum = magic + count + Σdata   (32-bit wrap)
```
Writing a slot: erase the slot range (512-byte steps), then program sequentially. Checksums of the dumped slots 0 and 2 verify correctly.

> **Page size:** every slot starts on a 512-byte boundary, which matches the FT32F030C8's 512-byte pages, so the slots never overlap. **Porting note:** on a genuine STM32F030x8 (1 KB pages), slots 0 and 1 would share the page 0x0800_F000–0x0800_F3FF, and saving either one would erase the other. For such a port, align the slots to 1 KB, e.g. settings 0x0800_F000, schedules 0x0800_F400–0x0800_FBFF, tag 0x0800_FC00.

**Slot 0 – settings (14 words, in this order):**

| Word | Field | Dump value |
|---|---|---|
| 0 | unknown byte (`0x2000091E`) | 0 |
| 1 | paired flag | 0 |
| 2 | DP 50 | 10 |
| 3 | DP 103 | 0 |
| 4 | DP 104 | 0 |
| 5 | DP 101 | 100 |
| 6 | DP 102 | 100 |
| 7 | DP 105 | 100 |
| 8 | DP 107 | 10 |
| 9 | DP 123 | 20 |
| 10 | DP 124 | 100 |
| 11 | DP 48 | 2 |
| 12 | DP 106 | 65 |
| 13 | DP 127 | 1 |

**Slot 1 – schedules:** for each of the 7 slots:
```
H = hdr_id<<24 | n<<16 | enabled<<8 | weekday_mask
then per entry (n entries):
  A = start_min<<16 | end_min
  B = autooff_s<<16 | (off_mode<<4 | mode)<<8 | preset
```
Total count = Σ(1 + 2n). **[C]** The second byte of H is the entry count n, not the DP payload length (4 + 9n) used in §13.2. Disabled slots are stored as H = 0. The stock default record is `0x0001017F 0x0000059F 0x003C2202`, then six zero headers.

**Slot 2 – tag:** one word per character. If the slot is invalid at boot, it is written with `"CF.20"` (5 chars).

**Save policy (`@0x08005286`):** dirty flags are latched with a 3 s debounce; the save happens when that timer expires, the link is idle, and any TRIAC fade has finished.

### 13.2 Schedule record (RAM `0x200006A8`, 7 × 0x2C bytes) and DP 116–122 payload **[C]**
```c
typedef struct { uint16_t start_min, end_min, autooff_s;
                 uint8_t mode, off_mode, preset, _pad; } sched_entry_t;   // 10 B
typedef struct { sched_entry_t e[4]; uint8_t _pad[8];
                 uint8_t hdr_id, count, enabled, weekday_mask; } sched_t; // 0x2C B
```
DP string = base64 of:
```
hdr_id, len(=4+9n), enabled, weekday_mask,
n × { start_hi, start_lo, end_hi, end_lo, mode, off_mode, preset, autooff_hi, autooff_lo }
```
- n ≤ 4. A payload shorter than 4 bytes disables the slot.
- base64 uses the standard alphabet (table at 0x0800_6EE0) with `=` padding.

| Field | Meaning |
|---|---|
| start/end | minutes since midnight; end < start = overnight |
| mode | 1 = switch on at start time; 2 = PIR-controlled while active |
| off_mode | 1 = switch off at end time; 2 = auto-off after `autooff_s` without motion or sound |
| preset | light gate: selects the dark threshold DP 107 / 123 / 124 (defaults 10 / 20 / 100) |
| weekday_mask | bit n = day n, day 0 = Sunday (Tuya weekday % 7) |

### 13.3 Built-in default record (0x0800_6EB0) **[C]**
Used whenever the device is **not paired** (no valid time):

| Field | Value |
|---|---|
| start – end | 00:00–23:59 |
| autooff | 60 s |
| mode | 2 (PIR) |
| off_mode | 2 (auto-off) |
| preset | 2 (threshold = DP 124, default 100) |
| enabled | 1 |
| weekday_mask | 0x7F (every day) |

It is also copied into schedule slot 0 on factory defaults.

### 13.4 Schedule evaluation (`@0x08004E2C`) **[C]**
Runs on minute boundaries once time is known.
- **Unpaired:** the active record is the default record.
- **Paired:** find the first enabled slot/entry whose weekday bit matches and `start ≤ now < end`. For overnight entries, `now ≥ start` today or `now < end` on the next day.
- **On match:**
  - apply the light preset,
  - if `mode == 1` and now == start → `switch(1, src=1)`,
  - arm a minute timer for the end time,
  - at the end, if `off_mode == 1` → `switch(0, src=1)`.
- **No match:** active record = none, so PIR is inactive and the LEDs are cyan.

---

## 14. Application logic

### 14.1 Switch function and trigger sources (`@0x08004ABC`) **[C]**
```c
void load_switch(bool on, uint8_t src) {
    dp1 = on;
    run_mode   = on ? 3 : 2;       // byte 0x2000091D
    load_state = on ? 6 : 3;       // byte 0x20000925
    last_src   = src;              // byte 0x20000920
    report(DP1);
}
```
| src | Origin |
|---|---|
| 0 | Manual: DP 1 from the app, PF7 short press, PB4 wall-switch change |
| 1 | Timer: schedule start/end, countdown expiry |
| 2 | Sensor: PIR (or DP 115 sound test) turn-on, inactivity auto-off |
| 3 | DP 115 test mode entered (forces off) |

### 14.2 Load state machine (`@0x08005300`, byte 0x20000925) **[C]**
| State | Meaning | Behaviour |
|---|---|---|
| 0 | Boot | If PF7 is held → factory test (8). Otherwise run_mode = 2; if not paired → start pairing (§14.3); go to 5. |
| 3 | Switch-off requested | `triac_set(0, DP104)`; snapshot dark flag. If the active record has mode 2: src 0 → hold-off timer DP 50 s → 4; src 3 → hold-off 3 s → 4; otherwise → 5. Update LED colour. |
| 4 | Hold-off | Wait for the timer → 5 (LED cyan meanwhile) |
| 5 | Off / armed | Re-render LEDs when the dark flag changes. DP 115 = 2 and motion → DP 115 = 1, `switch(1,2)`. DP 115 = 4 and sound → DP 115 = 3, `switch(1,2)`. Record mode 2 && dark && motion → `switch(1,2)`. |
| 6 | Switch-on requested | `triac_set(1, DP103)`. If the record has off_mode 2, start the auto-off timer (`autooff_s`) → 7. Update LED colour. |
| 7 | On | If the record has off_mode 2: motion or sound restarts the auto-off timer; on expiry → `switch(0,2)`. |
| 8–12 | Factory test | §14.6 |
| 13 | Halt | Factory test finished |

### 14.3 Wi-Fi pairing / connection supervisor (`@0x08005030`, byte 0x20000929) **[C]**
Supervisor states:

| State | Behaviour |
|---|---|
| 0 | When the module becomes alive (first heartbeat) → 0x16 |
| 0x16 | If Wi-Fi status ≠ 4, start a 20 s timer → 0x17 |
| 0x17 | Status 4 → 0x16. After 20 s: if paired, set LED bits 6–7 = 10 (yellow breathing, "connection lost"); start a 600 s timer → 0x18 |
| 0x18 | Status 4 → 0x16. After 600 s → reset the module (PB10 low 100 ms) → 0 |
| 0x19 | **Enter pairing:** 5 s timer, retries = 0, LED bits 6–7 = 01 (white blink), connected = 0 → 0x1A |
| 0x1A | After the timer: start a 180 s overall timer → 0x1B |
| 0x1B | Connected → 0x16. When the link is idle: retries++, send `0x05 [00]` → 0x1C. 180 s expiry → 0x1D |
| 0x1C | Connected → 0x16. Response received or retries ≥ 3 → 0x1D. Otherwise wait 10 s → 0x1A |
| 0x1D | Connected (status 4) → **load DP defaults, set paired = 1, save all, report all** → 0x1E. 180 s expired → connected = 2 (timed out) → 0x1E |
| 0x1E | Clear LED bits 6–7 → 0 |

"Connected" (byte 0x20000923): 0 = pairing, 1 = cloud connected, 2 = pairing timed out.

Pairing is entered:
- at boot if not paired,
- by holding PF7 for 6 s,
- when the module reports status 6.

Observed: white blink ≈1.5 Hz for ≈3 minutes.

### 14.4 DP 115 sensor test modes **[C]**
| Value | Set by | Behaviour |
|---|---|---|
| 0 | app / timeout | Normal |
| 1 | firmware | Result: motion detected (lamp switched on, src 2) |
| 2 | app | PIR test: `switch(0,3)`, LED bit 15 (green PB13 breathing); waits in state 5 for motion, independent of any schedule |
| 3 | firmware | Result: sound detected; DP 126 still reported each second |
| 4 | app | Sound test: like 2, but waits for the sound trigger; reports peak sound level (DP 126) each second |
| 5 | app | Light meter: report DP 125 each second |

Any write restarts a 900 s timeout; when it expires, DP 115 returns to 0 and LED bit 15 is cleared.

### 14.5 Inputs **[C]**
- **PF7 button** (`@0x08002B44`, 50 ms debounce):
  - short press (< 6 s) toggles the load, `switch(!dp1, 0)`,
  - hold 6 s → pairing (state 0x19),
  - held at boot → factory test.
- **PB4 wall switch** (`@0x08005EEC`) **[M]:** measures the mains-pulse activity on the switch leg against a quarter of the mains period, and derives a debounced "switch leg live" state. Every change toggles the load (src 0), which gives two-way switching. Unpopulated on this model; with the pull-up it stays constant, so it never toggles.

### 14.6 Factory test (`@0x080054D4`…) **[M]**
Entered by holding PF7 at power-up:
1. Load defaults and save; sound threshold 90.
2. Run an LED chase (one output every 400 ms) until PF7 is released.
3. Run the tests for up to 60 s; the LED word = `(~passed & 0xF) | 0x20`, so each LED stays lit until its test passes:

| Bit | LED | Test passes when |
|---|---|---|
| 0 | white | Wi-Fi test 0x0E reports signal ≥ 60 |
| 1 | red | PIR motion detected |
| 2 | green (PB13) | sound trigger fires |
| 3 | blue (PB14) | light level spans > 80 |

4. When all tests pass and PF7 is pressed: load test. Each press toggles the TRIAC and white LEDs at 100 %.
5. On timeout: show the result and halt (state 13).

---

## 15. Local time (`@0x08002E90`) **[C]**
- A seconds-of-week counter (0–604799), incremented every second.
- Request 0x1C when the link is idle: every 60 s once synced, every 3 s until synced.
- Response (data): `ok(1)=1, year, month, day, hour, min, sec, weekday(1..7, 7 = Sunday)`.
- `t = (weekday % 7)*86400 + hour*3600 + min*60 + sec`.
- The first sync sets the clock; later syncs correct drift.
- Helpers:
  - `minute_of_day = (t % 86400) / 60`
  - `day = t / 86400` (0 = Sunday)

---

## 16. Open items

| Item | Status |
|---|---|
| DP 111 purpose | ? stored and reported, use not found |
| Settings word 0 (byte 0x2000091E) | ? |
| Exact PIR lobe logic | M; follow `@0x08003CD0–0x08003E6A` |
| Wi-Fi status 6 handling details | M |
| PB4 wall-switch timing | M; not populated on this model |
| HS6605 pinout other than pin 8 | ? |

---

## 17. Recommendations for the rewrite

1. **Clock:** the original HSI/PREDIV × 6 setting is valid on the FT32F030C8. Use HSI/2 × 12 only if porting to a genuine STM32F030x8.
2. **Flash layout:** the original 512-byte-aligned layout is correct for the FT32F030C8; keep it, and the "NELE" record format, for compatibility with existing devices. Re-align to 1 KB only for an STM32 port.
3. **OTA:** keep the bootloader contract (staging at 0x0800_8000, marker 0x4E454C45 at 0x0800_EFFC) or replace it. Replace the 16-byte identity check with a header + CRC32.
4. **HS6605 timing:** the original uses calibrated busy-wait loops with interrupts disabled for the whole ~250 µs frame. The Cortex-M0 has no DWT cycle counter, so use a free-running timer for the delays, and keep the measured slot timings (≈1.1 µs low, ≈2.6 µs high, sample ≈9 µs after release, 13.25 µs per slot). Disabling interrupts for 250 µs delays TRIAC gate events by up to ≈4.5° at 50 Hz; this is acceptable, but interrupts could instead be disabled only within each slot.
5. **Watchdog:** keep the IWDG (≈10 s); the 100 ms blocking module reset is the longest blocking call.
6. **Product identity:** keep product key `urlxtrtlq9fjkv2d` (also at image offset 0x1000 / address 0x0800_2000 if the existing OTA path must accept the image) and the DP numbers/types for compatibility with the existing Tuya cloud product and app panel.

---

## Appendix A – Key RAM variables (original addresses, for cross-reference)

| Address | Content |
|---|---|
| 0x200001A4 | Clock `{sec, ms}` + last TIM6 CNT |
| 0x200001B0 | LED effect engine state |
| 0x200001DC | TRIAC/zero-cross state (state fn, table ptrs, period sum/avg, level, on, fade mode) |
| 0x20000214 | PIR lobe windows (4 × 0x1C) and sample counter |
| 0x20000294 | PIR state: sensitivity, flags, motion, state |
| 0x2000029C | ADC sampler (callbacks, state) |
| 0x200002A8 | ADC DMA buffer (512 × u16) |
| 0x20000180 | Light: level, hysteresis count/threshold, dark flag |
| 0x20000185 | Sound: level, threshold (DP 106), trigger |
| 0x200006A8 | Schedules (7 × 0x2C) |
| 0x200007DC | base64 scratch buffer |
| 0x200008DC | App: active record ptr, timers, DP 7, DP 111, DP 50, report-dirty mask (+0x2C), LED request word (+0x30) |
| 0x200008FC | DP values block: +0x12 DP1, +0x13 DP103, +0x14 DP104, +0x15 DP110, +0x16 DP101, +0x17 DP102, +0x18 DP105, +0x19 DP107, +0x1A DP123, +0x1B DP124, +0x1C DP115, +0x1D DP125, +0x1E DP126, +0x1F DP127 |
| 0x2000091C | App flags: +0 save flags, +1 run mode, +2 ?, +4 last src, +6 paired, +7 connected, +8 Wi-Fi status, +9 load state, +0xA test sub-state, +0xB schedule state, +0xD pairing state, +0xF report state |
| 0x2000092C | UART port objects (2 × 0x24) |
| 0x20000974 / 0x20000B74 | USART1 / USART2 RX DMA buffers |
| 0x20000D74 | Tuya: callbacks, port ptr, TX length/index |
| 0x20000D94 | Tuya: heartbeat timer, alive flag, pending cmd, response status, parser state |
| 0x20000DA4 | Tuya RX frame buffer |
| 0x20000EAF | Tuya TX frame buffer |
| 0x20000FBC | Flash storage slot table / cursor |
| 0x20000FE0 | OTA state |
| 0x20001000 | Local time state |
