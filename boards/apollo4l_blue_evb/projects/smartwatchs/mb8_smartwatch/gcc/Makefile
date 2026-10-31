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
GPU_CONVERT ?= 1
# Native experiment never links the vendor 32-bit LVGL renderer.
GPU_NATIVE_565 ?= 1
ifneq ($(filter $(GPU_NATIVE_565),0 1),$(GPU_NATIVE_565))
$(error GPU_NATIVE_565 must be 0 or 1)
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
# These project-local copies only change no-PSRAM allocation and font fallback.
LVGL_SRC := $(filter-out $(LVGL_DIR)/src/misc/lv_mem.c $(LVGL_DIR)/src/misc/lv_tlsf.c $(LVGL_DIR)/src/gpu/lv_gpu_ambiq_nema.c,$(LVGL_SRC))
FONT_OVERRIDES := $(notdir $(wildcard ../libs/gpu_port/lv_font_montserrat_*.c))
LVGL_SRC := $(filter-out $(addprefix $(LVGL_DIR)/src/font/,$(FONT_OVERRIDES)),$(LVGL_SRC))
PORT_SRC := $(wildcard ../libs/gpu_port/*.c)
ifeq ($(GPU_NATIVE_565),1)
PORT_SRC := $(filter-out ../libs/gpu_port/lv_gpu_ambiq_nema.c,$(PORT_SRC))
else
PORT_SRC := $(filter-out ../libs/gpu_port/watch_gpu_rgb565.c,$(PORT_SRC))
endif
AMBIQ_SRC := $(addprefix $(AMBIQ_DIR)/,lv_ambiq_nema_hal.c lv_ambiq_misc.c lv_ambiq_font_align.c lv_ambiq_decoder.c)
# The local watch_nema_hal.c fixes the SDK binary-semaphore ISR exit.
NEMA_SRC := $(addprefix $(NEMA_PLATFORM_DIR)/,nema_event.c nema_utils.c)
UTIL_SRC := $(addprefix $(ROOT_DIR)/utils/,am_util_delay.c am_util_faultisr.c am_util_id.c am_util_stdio.c am_util_string.c am_util_time.c)
FREERTOS_DIR := $(ROOT_DIR)/third_party/FreeRTOSv10.5.1/Source
RTOS_SRC := $(addprefix $(FREERTOS_DIR)/,queue.c tasks.c timers.c list.c event_groups.c)
RTOS_SRC += $(FREERTOS_DIR)/portable/GCC/AMapollo4/port.c $(FREERTOS_DIR)/portable/MemMang/heap_4.c
SRC := $(APP_SRC) $(PORT_SRC) $(LVGL_SRC) $(AMBIQ_SRC) $(NEMA_SRC) $(UTIL_SRC) $(RTOS_SRC) startup_gcc.c
# Strip leading ../ segments rather than allowing paths to escape CONFIG.
obj = $(CONFIG)/obj/$(subst ../,,$(1:.c=.o))
OBJS := $(foreach s,$(SRC),$(call obj,$(s)))

INCLUDE_DIRS := ../src ../libs/gpu_port $(ROOT_DIR) $(ROOT_DIR)/CMSIS/AmbiqMicro/Include $(ROOT_DIR)/CMSIS/ARM/Include ../../../../bsp $(ROOT_DIR)/devices $(ROOT_DIR)/utils $(ROOT_DIR)/mcu/apollo4l $(ROOT_DIR)/mcu/apollo4l/hal $(ROOT_DIR)/mcu/apollo4l/hal/mcu $(FREERTOS_DIR)/include $(FREERTOS_DIR)/portable/GCC/AMapollo4
INCLUDE_DIRS += $(LVGL_DIR) $(LVGL_DIR)/src $(LVGL_DIR)/src/misc $(LVGL_DIR)/src/gpu $(AMBIQ_DIR)
INCLUDE_DIRS += $(NEMA_DIR)/NemaGFX_SDK/NemaGFX $(NEMA_DIR)/NemaGFX_SDK/common/mem $(NEMA_DIR)/NemaGFX_SDK/include/tsi/NemaGFX $(NEMA_DIR)/NemaGFX_SDK/include/tsi/NemaDC $(NEMA_DIR)/NemaGFX_SDK/include/tsi/common $(NEMA_PLATFORM_DIR)
INCLUDE_DIRS += $(APP_DIRS)
CPPFLAGS := $(addprefix -I,$(INCLUDE_DIRS)) -DPART_apollo4l -DAM_PART_APOLLO4L -DAM_PACKAGE_BGA -DAM_FREERTOS -Dgcc
CPPFLAGS += -DWATCH_NATIVE_RGB565=$(GPU_NATIVE_565) -DWATCH_GPU_PROFILE=$(GPU_PROFILE) -DWATCH_GPU_CONVERT=$(GPU_CONVERT) -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DNEMA_PLATFORM=apollo4l_nemagfx -DNEMA_USE_CUSTOM_MALLOC -D'NEMA_CUSTOM_MALLOC_INCLUDE="lv_ambiq_nema_hal.h"' -DWAIT_IRQ_BINARY_SEMAPHORE=1
ARCH := -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS := $(ARCH) -std=gnu11 -O2 -g3 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := $(ARCH) -nostartfiles -static -Tlinker_script.ld -Wl,--gc-sections,--entry,Reset_Handler,-Map,$(CONFIG)/$(TARGET).map,--print-memory-usage
HAL_LIB := $(ROOT_DIR)/mcu/apollo4l/hal/mcu/gcc/bin/libam_hal.a
BSP_LIB := ../../../../bsp/gcc/bin/libam_bsp.a
GPU_LIB ?= $(AMBIQ_DIR)/gpu_lib_apollo4/gcc/bin/lvgl_ambiq_porting.a
ifeq ($(GPU_NATIVE_565),1)
# Deliberately exclude the incompatible 32-bit LVGL archive, even if supplied.
override GPU_LIB :=
endif
NEMA_LIB ?= $(NEMA_PLATFORM_DIR)/gcc/bin/lib_nema_apollo4l_nemagfx.a

.PHONY: all objects clean check-libs FORCE
FORCE:
$(CONFIG)/.compiler-flags: FORCE
	@mkdir -p $(CONFIG)
	@printf '%s\n' 'GPU_NATIVE_565=$(GPU_NATIVE_565) GPU_PROFILE=$(GPU_PROFILE) GPU_CONVERT=$(GPU_CONVERT) TOOLCHAIN=$(TOOLCHAIN) CFLAGS=$(CFLAGS)' > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@


all: $(CONFIG)/$(TARGET).bin
objects: $(OBJS)
check-libs:
	@missing=0; for lib in $(GPU_LIB) $(NEMA_LIB); do if [ ! -f "$$lib" ]; then echo "Missing vendor GPU library: $$lib" >&2; missing=1; fi; done; [ $$missing -eq 0 ] || { echo "Restore these from the licensed AmbiqSuite R4.5.0 package. No stubs are used." >&2; exit 1; }

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
$(CONFIG)/$(TARGET).axf: $(OBJS) $(HAL_LIB) $(BSP_LIB) | check-libs
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -Wl,--start-group $(GPU_LIB) $(NEMA_LIB) $(HAL_LIB) $(BSP_LIB) -lm -lc -lgcc -lnosys -Wl,--end-group
	$(SIZE) $@
$(CONFIG)/$(TARGET).bin: $(CONFIG)/$(TARGET).axf
	$(OBJCOPY) -O binary $< $@
clean:
	rm -rf $(CONFIG)
-include $(OBJS:.o=.d)
