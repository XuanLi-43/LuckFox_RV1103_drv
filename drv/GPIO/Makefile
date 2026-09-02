KERNEL_DIR=$(abspath $(CURDIR)/../../../source/kernel)

KERNEL_OUT=$(abspath $(CURDIR)/../../../source/objs_kernel)

CROSS_COMPILE ?= $(shell command -v arm-rockchip830-linux-uclibcgnueabihf-gcc >/dev/null 2>&1 \
	&& echo arm-rockchip830-linux-uclibcgnueabihf- \
	|| echo $(HOME)/Tools/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-)

# ARCH  目标架构:32 位 ARM 
ARCH=arm
export ARCH CROSS_COMPILE

obj-m := gpio.o
gpio-objs := gpio.o 

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


