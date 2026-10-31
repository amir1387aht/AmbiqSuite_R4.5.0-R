# Apollo4 Blue Lite / Mi Band 8: SDK-matched LVGL 8.1 + NemaP GPU.
.DEFAULT_GOAL := all
ROOT_DIR := ../../../../../..
TARGET := mb8_smartwatch
CONFIG ?= bin
TOOLCHAIN ?= arm-none-eabi
CC := $(TOOLCHAIN)-gcc
OBJCOPY := $(TOOLCHAIN)-objcopy
SIZE := $(TOOLCHAIN)-size
JOBS ?= 8
GPU_PROFILE ?= 0
WATCH_NAND_YAFFS2 ?= 0
WATCH_NAND_MSPI_QUAD ?= 1
ifneq ($(filter $(WATCH_NAND_MSPI_QUAD),0 1),$(WATCH_NAND_MSPI_QUAD))
$(error WATCH_NAND_MSPI_QUAD must be 0 or 1)
endif
ifneq ($(filter $(WATCH_NAND_YAFFS2),0 1),$(WATCH_NAND_YAFFS2))
$(error WATCH_NAND_YAFFS2 must be 0 or 1)
endif
# GPU_CONVERT remains an accepted alias for existing flash commands.
GPU_COPY ?= $(if $(GPU_CONVERT),$(GPU_CONVERT),1)
ifneq ($(filter $(GPU_COPY),0 1),$(GPU_COPY))
$(error GPU_COPY must be 0 or 1)
endif
ifneq ($(GPU_NATIVE_565),)
ifneq ($(GPU_NATIVE_565),1)
$(error Main is native RGB565 only; use Git history for the old BGRA reference)
endif
endif
SHELL := /bin/bash

LVGL_DIR := $(ROOT_DIR)/third_party/LVGL/lvgl
AMBIQ_DIR := $(ROOT_DIR)/third_party/LVGL/ambiq_support
NEMA_DIR := $(ROOT_DIR)/third_party/ThinkSi
NEMA_PLATFORM_DIR := $(NEMA_DIR)/config/apollo4l_nemagfx

# Full paths preserve source identity; never compile the copied 8.4 tree.
# Enumerate with GNU make, not shell find: Windows find.exe and shell quoting
# can silently return an empty list, dropping main(), RTOS hooks and LVGL.
source_dirs = $(1) $(foreach d,$(wildcard $(1)/*/),$(call source_dirs,$(patsubst %/,%,$(d))))
APP_DIRS := $(call source_dirs,../src)
LVGL_DIRS := $(call source_dirs,$(LVGL_DIR)/src)
APP_SRC := $(sort $(foreach d,$(APP_DIRS),$(wildcard $(d)/*.c)))
LVGL_SRC := $(sort $(foreach d,$(LVGL_DIRS),$(wildcard $(d)/*.c)))
ifeq ($(filter ../src/main.c ../src/rtos.c,$(APP_SRC)),../src/main.c ../src/rtos.c)
else
$(error Application source discovery failed: expected ../src/main.c and ../src/rtos.c. Build from this project's gcc directory)
endif
ifeq ($(filter $(LVGL_DIR)/src/core/lv_obj.c,$(LVGL_SRC)),)
$(error SDK LVGL source discovery failed: expected $(LVGL_DIR)/src/core/lv_obj.c)
endif
# Project-local allocation adapters share app heaps; fonts use the SDK ABI.
LVGL_SRC := $(filter-out $(LVGL_DIR)/src/misc/lv_mem.c $(LVGL_DIR)/src/misc/lv_tlsf.c $(LVGL_DIR)/src/gpu/lv_gpu_ambiq_nema.c,$(LVGL_SRC))
FONT_OVERRIDES := $(notdir $(wildcard ../libs/gpu_port/lv_font_montserrat_*.c))
LVGL_SRC := $(filter-out $(addprefix $(LVGL_DIR)/src/font/,$(FONT_OVERRIDES)),$(LVGL_SRC))
PORT_SRC := $(wildcard ../libs/gpu_port/*.c)
# Exclude removed legacy names in an existing working tree as well.
PORT_SRC := $(filter-out ../libs/gpu_port/lv_gpu_ambiq_nema.c ../libs/gpu_port/watch_gpu_convert.c,$(PORT_SRC))
AMBIQ_SRC := $(addprefix $(AMBIQ_DIR)/,lv_ambiq_nema_hal.c lv_ambiq_misc.c lv_ambiq_font_align.c lv_ambiq_decoder.c)
# The local watch_nema_hal.c fixes the SDK binary-semaphore ISR exit.
NEMA_SRC := $(addprefix $(NEMA_PLATFORM_DIR)/,nema_event.c nema_utils.c)
UTIL_SRC := $(addprefix $(ROOT_DIR)/utils/,am_util_delay.c am_util_faultisr.c am_util_id.c am_util_stdio.c am_util_string.c am_util_time.c)
FREERTOS_DIR := $(ROOT_DIR)/third_party/FreeRTOSv10.5.1/Source
RTOS_SRC := $(addprefix $(FREERTOS_DIR)/,queue.c tasks.c timers.c list.c event_groups.c)
RTOS_SRC += $(FREERTOS_DIR)/portable/GCC/AMapollo4/port.c $(FREERTOS_DIR)/portable/MemMang/heap_4.c
SRC := $(APP_SRC) $(PORT_SRC) $(LVGL_SRC) $(AMBIQ_SRC) $(NEMA_SRC) $(UTIL_SRC) $(RTOS_SRC) startup_gcc.c
# Additive storage port: no startup calls and no LVGL/display dependencies.
ifeq ($(WATCH_NAND_YAFFS2),1)
NAND_SRC := $(wildcard ../storage/*.c ../storage/yaffs2/*.c)
ifeq ($(WATCH_NAND_MSPI_QUAD),1)
NAND_SRC := $(filter-out ../storage/mb8_nand_iom.c,$(NAND_SRC))
else
NAND_SRC := $(filter-out ../storage/mb8_nand_mspi.c ../storage/mb8_nand_mspi_plan.c,$(NAND_SRC))
endif
SRC += $(NAND_SRC)
endif
# Strip leading ../ segments rather than allowing paths to escape CONFIG.
obj = $(CONFIG)/obj/$(subst ../,,$(1:.c=.o))
OBJS := $(foreach s,$(SRC),$(call obj,$(s)))

INCLUDE_DIRS := ../src ../libs/gpu_port $(ROOT_DIR) $(ROOT_DIR)/CMSIS/AmbiqMicro/Include $(ROOT_DIR)/CMSIS/ARM/Include ../../../../bsp $(ROOT_DIR)/devices $(ROOT_DIR)/utils $(ROOT_DIR)/mcu/apollo4l $(ROOT_DIR)/mcu/apollo4l/hal $(ROOT_DIR)/mcu/apollo4l/hal/mcu $(FREERTOS_DIR)/include $(FREERTOS_DIR)/portable/GCC/AMapollo4
INCLUDE_DIRS += $(LVGL_DIR) $(LVGL_DIR)/src $(LVGL_DIR)/src/misc $(LVGL_DIR)/src/gpu $(AMBIQ_DIR)
INCLUDE_DIRS += $(NEMA_DIR)/NemaGFX_SDK/NemaGFX $(NEMA_DIR)/NemaGFX_SDK/common/mem $(NEMA_DIR)/NemaGFX_SDK/include/tsi/NemaGFX $(NEMA_DIR)/NemaGFX_SDK/include/tsi/NemaDC $(NEMA_DIR)/NemaGFX_SDK/include/tsi/common $(NEMA_PLATFORM_DIR)
INCLUDE_DIRS += $(APP_DIRS) ../storage
CPPFLAGS := $(addprefix -I,$(INCLUDE_DIRS)) -DPART_apollo4l -DAM_PART_APOLLO4L -DAM_PACKAGE_BGA -DAM_FREERTOS -Dgcc
CPPFLAGS += -DWATCH_NATIVE_RGB565=1 -DWATCH_GPU_PROFILE=$(GPU_PROFILE) -DWATCH_GPU_COPY=$(GPU_COPY) -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DNEMA_PLATFORM=apollo4l_nemagfx -DNEMA_USE_CUSTOM_MALLOC -D'NEMA_CUSTOM_MALLOC_INCLUDE="lv_ambiq_nema_hal.h"' -DWAIT_IRQ_BINARY_SEMAPHORE=1
CPPFLAGS += -DWATCH_NAND_YAFFS2=$(WATCH_NAND_YAFFS2) -DWATCH_NAND_MSPI_QUAD=$(WATCH_NAND_MSPI_QUAD)
ifeq ($(WATCH_NAND_YAFFS2),1)
CPPFLAGS += -I../storage/yaffs2 -DCONFIG_YAFFS_DIRECT -DCONFIG_YAFFS_YAFFS2
CPPFLAGS += -DCONFIG_YAFFS_DEFINES_TYPES -DCONFIG_YAFFS_PROVIDE_DEFS -DCONFIG_YAFFSFS_PROVIDE_VALUES
CPPFLAGS += -DCONFIG_YAFFS_SMALL_RAM -DCONFIG_YAFFS_SHORT_NAMES_IN_RAM -DCONFIG_YAFFS_USE_32_BIT_TIME_T
CPPFLAGS += -DCONFIG_YAFFS_NO_TRACE -D'Y_LOFF_T=long long'
endif
ARCH := -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS := $(ARCH) -std=gnu11 -O2 -g3 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := $(ARCH) -nostartfiles -static -Tlinker_script.ld -Wl,--gc-sections,--entry,Reset_Handler,-Map,$(CONFIG)/$(TARGET).map,--print-memory-usage
ifeq ($(WATCH_NAND_YAFFS2),1)
# Keep opt-in public entry points link-checked without adding startup activity.
LDFLAGS += -Wl,--undefined=mb8_storage_mount,--undefined=mb8_storage_unmount
LDFLAGS += -Wl,--undefined=yaffs_open,--undefined=yaffs_read,--undefined=yaffs_write,--undefined=yaffs_fsync,--undefined=yaffs_close
endif
HAL_LIB := $(ROOT_DIR)/mcu/apollo4l/hal/mcu/gcc/bin/libam_hal.a
BSP_LIB := ../../../../bsp/gcc/bin/libam_bsp.a
NEMA_LIB ?= $(NEMA_PLATFORM_DIR)/gcc/bin/lib_nema_apollo4l_nemagfx.a

.PHONY: all objects clean check-libs FORCE
FORCE:
$(CONFIG)/.compiler-flags: FORCE
	@mkdir -p $(CONFIG)
	@printf '%s\n' 'NATIVE_RGB565=1 GPU_PROFILE=$(GPU_PROFILE) GPU_COPY=$(GPU_COPY) TOOLCHAIN=$(TOOLCHAIN) WATCH_NAND_YAFFS2=$(WATCH_NAND_YAFFS2) WATCH_NAND_MSPI_QUAD=$(WATCH_NAND_MSPI_QUAD) CFLAGS=$(CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@


all: $(CONFIG)/$(TARGET).bin
objects: $(OBJS)
check-libs:
	@missing=0; for lib in $(NEMA_LIB); do if [ ! -f "$$lib" ]; then echo "Missing vendor GPU library: $$lib" >&2; missing=1; fi; done; [ $$missing -eq 0 ] || { echo "Restore these from the licensed AmbiqSuite R4.5.0 package. No stubs are used." >&2; exit 1; }

define compile_rule
$(call obj,$(1)): $(1) Makefile $(CONFIG)/.compiler-flags
	@mkdir -p $$(@D)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(SRC),$(eval $(call compile_rule,$(s))))

$(HAL_LIB):
	$(MAKE) -C $(ROOT_DIR)/mcu/apollo4l/hal/mcu/gcc -j$(JOBS)
$(BSP_LIB):
	$(MAKE) -C ../../../../bsp/gcc -j$(JOBS)
$(CONFIG)/$(TARGET).axf: $(OBJS) linker_script.ld $(HAL_LIB) $(BSP_LIB) | check-libs
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -Wl,--start-group $(NEMA_LIB) $(HAL_LIB) $(BSP_LIB) -lm -lc -lgcc -lnosys -Wl,--end-group
	$(SIZE) $@
$(CONFIG)/$(TARGET).bin: $(CONFIG)/$(TARGET).axf
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(CONFIG)
-include $(OBJS:.o=.d)
