KERNEL_DIR ?= $(abspath $(CURDIR)/../../../source/kernel)

KERNEL_OUT ?= $(abspath $(CURDIR)/../../../source/objs_kernel)

CROSS_COMPILE ?= $(shell command -v arm-rockchip830-linux-uclibcgnueabihf-gcc >/dev/null 2>&1 \
	&& echo arm-rockchip830-linux-uclibcgnueabihf- \
	|| echo $(HOME)/Tools/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-)

# ARCH  目标架构:32 位 ARM 
ARCH=arm
export ARCH CROSS_COMPILE

# 多文件模块：一个 .ko，由三个 .c 编译链接而成
obj-m := md0240_lcd.o
md0240_lcd-objs := md0240_lcd_main.o md0240_lcd_ctrl.o md0240_lcd_fb.o

all:
	$(MAKE) -C $(KERNEL_DIR) O=$(KERNEL_OUT) M=$(CURDIR) quiet= modules
	@mkdir -p ko obj
	@mv -f *.ko ko/ 2>/dev/null || true
	@mv -f *.o *.mod *.mod.c *.mod.o .*.cmd Module.symvers modules.order modules.builtin obj/ 2>/dev/null || true
	@echo "KO files moved to $(CURDIR)/ko/, objects moved to $(CURDIR)/obj/"

# clean  清理本模块的编译产物
clean:
	$(MAKE) -C $(KERNEL_DIR) O=$(KERNEL_OUT) M=$(CURDIR) clean
	@rm -rf ko obj


