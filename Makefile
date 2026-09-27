# Makefile for FT32F030C8AT7 (Cortex-M0, 64 KB flash, 8 KB SRAM)

TARGET   := pir-switch
BUILD    := build

# Standard peripheral drivers to build. Each entry compiles
# ft32f0xx_<name>.c and defines RTE_DEVICE_<NAME> so the pack's
# ft32f0xx_conf.h pulls in the matching header.
DRIVERS  := rcc gpio misc

DEVICE   := FT32F030x8
LDSCRIPT := linker/FT32F030x8.ld

DFP      := vendor/FT32F0xx_DFP
CMSIS    := $(DFP)/Drivers/CMSIS/FT32F0xx
SPL      := $(DFP)/Drivers/FT32F0xx_Driver

C_SRCS   := $(wildcard src/*.c) \
            $(addprefix $(SPL)/Src/ft32f0xx_,$(addsuffix .c,$(DRIVERS)))
ASM_SRCS := startup/startup_ft32f030x8.s

INCLUDES := -Isrc -I$(CMSIS)/Include -I$(SPL)/Inc

DEFS     := -D$(DEVICE) $(foreach d,$(DRIVERS),-DRTE_DEVICE_$(shell echo $(d) | tr a-z A-Z))

CROSS    ?= arm-none-eabi-
CC       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
SIZE     := $(CROSS)size

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

# Flashing via OpenOCD. The FT32F0 flash controller is STM32F0-compatible;
# override OPENOCD_CFG if your probe or target config differs.
OPENOCD     ?= openocd
OPENOCD_CFG ?= -f interface/stlink.cfg -f target/stm32f0x.cfg

.PHONY: all clean flash size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) $<

flash: $(BUILD)/$(TARGET).elf
	$(OPENOCD) $(OPENOCD_CFG) -c "program $< verify reset exit"

clean:
	rm -rf $(BUILD)

-include $(DEPS)
