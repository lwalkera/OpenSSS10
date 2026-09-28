# Makefile for FT32F030C8AT7 (Cortex-M0, 64 KB flash, 8 KB SRAM)
#
# The image is linked at 0x08001000 and runs behind the stock bootloader
# (0x08000000-0x08000FFF); see linker/FT32F030x8.ld.

TARGET   := pir-switch
BUILD    := build

# Optional features, each enabled with -DFEATURE_<NAME>:
#   FACTORY_TEST  PF7 held at power-up enters the factory test
#   WALLSWITCH    PB4 three-way wall-switch input
#   TELEMETRY     USART2 PIR sample stream at 115200 baud
#   SENSOR_TEST   DP 115 sensor test modes (and DPs 125/126)
FEATURES ?= FACTORY_TEST WALLSWITCH TELEMETRY SENSOR_TEST

# Standard peripheral drivers to build. Each entry compiles
# ft32f0xx_<name>.c and defines RTE_DEVICE_<NAME> so the pack's
# ft32f0xx_conf.h pulls in the matching header. Everything else is
# programmed at register level; the flash driver is used because it
# carries the FT32-specific programming sequence.
DRIVERS  := flash

DEVICE   := FT32F030x8
LDSCRIPT := linker/FT32F030x8.ld
APP_BASE := 0x08001000

DFP      := vendor/FT32F0xx_DFP
CMSIS    := $(DFP)/Drivers/CMSIS/FT32F0xx
SPL      := $(DFP)/Drivers/FT32F0xx_Driver

C_SRCS   := $(wildcard src/*.c) \
            $(addprefix $(SPL)/Src/ft32f0xx_,$(addsuffix .c,$(DRIVERS)))
ASM_SRCS := startup/startup_ft32f030x8.s

INCLUDES := -Isrc -I$(CMSIS)/Include -I$(SPL)/Inc

upper     = $(shell echo $(1) | tr a-z A-Z)
DEFS     := -D$(DEVICE) \
            $(foreach d,$(DRIVERS),-DRTE_DEVICE_$(call upper,$(d))) \
            $(foreach f,$(FEATURES),-DFEATURE_$(f))

CROSS    ?= arm-none-eabi-
CC       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size
PYTHON   ?= python3

OPT      ?= -Os
ARCH     := -mcpu=cortex-m0 -mthumb -mfloat-abi=soft

CFLAGS   := $(ARCH) $(OPT) -g3 -std=gnu11 -Wall -Wextra \
            -ffunction-sections -fdata-sections \
            $(DEFS) $(INCLUDES) -MMD -MP
ASFLAGS  := $(ARCH) -g3 -x assembler-with-cpp
LDFLAGS  := $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
            -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--print-memory-usage \
            --specs=nano.specs --specs=nosys.specs -nostartfiles

OBJS     := $(addprefix $(BUILD)/,$(C_SRCS:.c=.o) $(ASM_SRCS:.s=.o))
DEPS     := $(OBJS:.o=.d)

# OpenOCD. Override OPENOCD_CFG for other probes.
#
# Every target starts from `reset halt`: the core stops at the bootloader's
# reset vector before any firmware runs, so no watchdog or DMA is active.
OPENOCD      ?= openocd
# SWD clock in kHz (make SWD_KHZ=100 ...); lower it on long or noisy wiring
SWD_KHZ      ?= 1000
OPENOCD_CFG  ?= -f interface/cmsis-dap.cfg -c "set FT32_SWD_KHZ $(SWD_KHZ)" -f openocd/ft32f0x.cfg
OPENOCD_HALT := -c init -c "reset halt"

# Host build of the hardware-independent modules for unit tests
HOST_CC    ?= cc
TEST_SRCS  := $(wildcard test/*.c) \
              src/base64.c src/crc32.c src/pir_detect.c src/record.c \
              src/sched_codec.c src/tables.c src/tuya_frame.c
TEST_BIN   := $(BUILD)/host/run_tests

.PHONY: all clean flash size backup test tables bridge bridge-run

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

# The vendor drivers have harmless unused variables
$(BUILD)/$(DFP)/%.o: CFLAGS += -Wno-unused-variable

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

# --print-memory-usage goes to a file so `size` can show it at the end
$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@ > $(BUILD)/$(TARGET).mem

# The .bin gets its OTA header (length, CRC32) patched in; the .hex is made
# from the patched .bin so both carry a valid header.
$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf tools/mkimage.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/mkimage.py $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).bin
	$(OBJCOPY) -I binary -O ihex --change-addresses $(APP_BASE) $< $@

# Runs last: after the .hex and .bin are written
size: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	@$(SIZE) $<
	@echo
	@cat $(BUILD)/$(TARGET).mem

# Uses the FT32 flash commands from openocd/ft32f0x.cfg (512-byte pages).
# Writes only the application area, then resets into the new firmware.
flash: $(BUILD)/$(TARGET).bin
	$(OPENOCD) $(OPENOCD_CFG) $(OPENOCD_HALT) \
	  -c "ft32_flash_write_bin $< $(APP_BASE)" -c "reset run" -c shutdown

# Dump the whole 64 KB flash (bootloader, app, settings) before flashing
backup:
	@mkdir -p backup
	$(OPENOCD) $(OPENOCD_CFG) $(OPENOCD_HALT) \
	  -c "dump_image backup/flash-$$(date +%Y%m%d-%H%M%S).bin 0x08000000 0x10000" \
	  -c resume -c shutdown

tables:
	$(PYTHON) tools/gen_tables.py > src/tables.c

# Wi-Fi UART bridge (bridge/bridge.c): runs from SRAM, never touches flash.
# bridge-run stops the core at the reset vector, loads and verifies the
# bridge, and starts it in thread mode with interrupts masked. Reset or
# power-cycle the board to get the firmware back.
BRIDGE := $(BUILD)/bridge/bridge.elf

bridge: $(BRIDGE)

$(BRIDGE): bridge/bridge.c bridge/bridge.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Os -g3 -std=gnu11 -Wall -Wextra -ffunction-sections \
	  -ffreestanding -fno-tree-loop-distribute-patterns \
	  -D$(DEVICE) $(INCLUDES) -nostartfiles -nostdlib -T bridge/bridge.ld \
	  -Wl,--gc-sections -Wl,--print-memory-usage $< -o $@

# OpenOCD's work area (used by verify_image) defaults to 0x20000000, where the
# bridge loads; move it to 0x20001000-0x200017FF, which the bridge leaves free.
bridge-run: $(BRIDGE)
	$(OPENOCD) $(OPENOCD_CFG) \
	  -c "ft32f0x.cpu configure -work-area-phys 0x20001000 -work-area-size 0x800" \
	  $(OPENOCD_HALT) \
	  -c "load_image $<" -c "verify_image $<" \
	  -c "reg xpsr 0x01000000" -c "reg control 0" -c "reg primask 1" \
	  -c "reg msp 0x20002000" -c "resume 0x20000000" -c shutdown

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(wildcard src/*.h test/*.h) Makefile
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -Wall -Wextra -g -Isrc -Itest $(TEST_SRCS) -lm -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)
