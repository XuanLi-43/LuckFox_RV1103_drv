# MD0240 LCD 驱动（ST7789V）

正点原子ATK MD0240 屏（ST7789V，240×320 竖屏，RGB565，SPI 模式 0）在 Luckfox Pico Plus 上的 framebuffer 驱动。

| 项 | 值 |
|---|---|
| 屏幕 | ST7789V，240×320 竖屏，RGB565 |
| MADCTL (0x36) | `0x00`（竖屏） |
| SPI | 模式 0，**40MHz**（probe 硬编码 `spi->max_speed_hz = 40000000`，见 `md0240_lcd_main.c`；**优先于 dts**，花屏降到 30MHz） |
| GPIO（dts） | `dc-gpios` RK_PC7 高=数据 / 低=命令；`rst-gpios` RK_PC6 低=复位；`pwr-gpios` RK_PC5 高=开背光 |
| 片选 | SPI 控制器自动管理（spi0m0_cs0），驱动不碰 |
| 字节序 | 内存低字节在前 → 屏上高字节先行（驱动 blit 做交换，用户态不用操心） |

## 文件说明

```
MD0240_LCD/
├── md0240_lcd_main.c   ← 设备匹配（compatible="atk_md0240,md0240_lcd"）、probe/remove、spi 频率
├── md0240_lcd_ctrl.c   ← 硬件操作：写命令/数据、复位时序、初始化序列、blit/fill、分块 SPI 发送
├── md0240_lcd_fb.c     ← framebuffer 操作集 + deferred_io 脏行刷屏（刷新引擎）
├── md0240_lcd.h
├── Makefile
└── rv1103g-luckfox-pico-plus.dts   ← 完整板级设备树（改动点见下文"设备树"）
```

数据流：用户程序（fbcon / mmap / write / LVGL）→ 显存影子拷贝 vmem（"草稿纸"）→ 脏行记账（16ms 批量刷新）→ blit 字节序交换 → 分块 SPI → 屏幕。

## 放置与编译

通用规则（`my_drv/` 需自建、软链方式、KERNEL_DIR 覆盖等）见**仓库根 README 第一节**。这里只列命令。

```bash
# 放置（二选一）
cp -r drv/MD0240_LCD <SDK>/sysdrv/drv_ko/my_drv/
# 或 ln -s /path/to/rv1103_drv/drv/MD0240_LCD <SDK>/sysdrv/drv_ko/my_drv/MD0240_LCD

# 编译（在 SDK 树内路径下执行）
cd <SDK>/sysdrv/drv_ko/my_drv/MD0240_LCD
make                    # 产物 ko/md0240_lcd.ko

# 清理
make clean
```

## 导入开发板并使用

```bash
scp ko/md0240_lcd.ko root@<开发板IP>:/root/

# 板子上：
insmod /root/md0240_lcd.ko
ls /dev/fb*                        # → /dev/fb0
dmesg | grep md0240                # probe / 注册日志
lsmod | grep md0240

head -c 153600 /dev/urandom > /dev/fb0   # 240*320*2 字节 → 满屏雪花，基本自检

rmmod md0240_lcd                   # 卸载后内核弹回默认 fbcon（空白/乱码画面属正常）
```

**用户态程序独占屏幕时**（如 LVGL）：若 console 绑定了 fbcon，程序启动前先解绑：

```sh
echo 0 > /sys/class/vtconsole/vtcon1/bind
```

> LVGL 接入侧注意：`LV_COLOR_16_SWAP` 必须保持 0（驱动 blit 已做字节序交换，双交换颜色必乱）；`LV_TICK_CUSTOM` 必须开，否则画面不动。

## 设备树

本目录的 `rv1103g-luckfox-pico-plus.dts` 是 **SDK 原版板级 dts 的本地修改版**，与上游差异只有两处：

1. 根节点下 `/delete-node/ dht11_sensor;` —— 原厂 dht11 占用的 GPIO1_C7 与本模块 DC 引脚冲突，删除；
2. `/**********MD0240_LCD**********/` 段 —— `&spi0` 打开并使能 `md0240_lcd@0` 节点（`dc-gpios` / `rst-gpios` / `pwr-gpios`）。

其余内容与 SDK 一致（升级 SDK 后注意核对）。

> dts 里的 `spi-max-frequency` 仅作板级声明，**实际频率以驱动为准**（probe 硬编码 40MHz）。改频率要改 `md0240_lcd_main.c`，花屏时降到 30MHz 重编，改 dts 无效。

改动生效：dts → 覆盖内核树 → Docker 容器编 boot.img → SocToolKit 只烧 boot 分区，全流程见仓库根 [docs/设备树编译与烧录指南.md](../../docs/设备树编译与烧录指南.md)。


