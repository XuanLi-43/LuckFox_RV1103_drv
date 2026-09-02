# rv1103_drv — Luckfox Pico 系列内核驱动模块

针对 **Luckfox Pico 系列（RV1103 / RV1106）SDK** 的内核驱动模块仓库。

**每个模块的具体说明（硬件接线、编译、烧录、坑）见各自文件夹内的 `README.md`**，本文件只讲通用规则。

当前收录：

| 模块 | 说明 | 硬性地址 |
|---|---|---|
| `drv/MD0240_LCD/` | MD0240 屏（ST7789V 240×320 SPI）framebuffer 驱动 | 详见模块内 README.md |
| `drv/GPIO/` | GPIO 引脚控制字符设备驱动（方向/上下拉/输出电平） | 暂无模块内 README（说明见下文"目录结构"） |

## 目录结构

```
rv1103_drv/
├── README.md                     ← 本文：所有模块通用的放置/编译/上传规则
├── drv/
│   ├── MD0240_LCD/
│   │   ├── README.md             ← 该模块的完整说明（硬件、dts、坑……）
│   │   ├── Makefile              ← 模块构建脚本（所有模块共用同一套规则）
│   │   ├── 模块源码 *.c / *.h
│   │   └── *-pico-plus.dts       ← 需要的设备树（如涉及 GPIO/SPI 等外围时需要）
│   ├── GPIO/
│   │   ├── Makefile              ← 模块构建脚本（所有模块共用同一套规则）
│   │   ├── gpio.c                ← GPIO 控制驱动源码（暂无独立 README，说明见各目录注释）
│   │   └── rv1103g-luckfox-pico-plus.dts  ← 设备树（GPIO3_C6 及 pinctrl 状态）
│   └── <新模块>/                 ← 以后按同样结构添加（每个模块自含 README + Makefile）
└── docs/
    └── 设备树编译与烧录指南.md   ← dts → boot.img → 烧录 → 验证（跨模块通用）
```

编译产物由各模块的 `.gitignore` 排除（`ko/`、`obj/`、`*.o`、`*.ko` 等），仓库只存源码。

## 官方参考

| 资源 | 链接 |
|---|---|
| SDK 编译镜像（Docker 环境）教程 | https://wiki.luckfox.com/zh/Luckfox-Pico-Plus-Mini/Docker-Image-Build |
| SDK 仓库（Gitee，推荐） | https://gitee.com/LuckfoxTECH/luckfox-pico.git |
| SDK 仓库（GitHub） | https://github.com/LuckfoxTECH/luckfox-pico.git |

**Docker 用于编译 boot.img / 整机镜像；内核模块本身不需要 Docker**（见下文"编译"）。

### Docker 教程速览（完整步骤看原链接）

```bash
# ① 获取镜像（联网）或离线 tar：
docker pull luckfoxtech/luckfox_pico:1.0
docker load -i ./luckfox_pico_docker.tar     # 离线 tar 方式

# ② 创建容器（把 SDK 挂到容器内 /home；容器名定为 luckfox）
sudo docker run -it --name luckfox --privileged \
  -v /path/to/luckfox-pico:/home luckfoxtech/luckfox_pico:1.0 /bin/bash

# ③ 容器已存在时，直接启动进入：
sudo docker start -ai luckfox

# ④ 容器内选择板型并编译（每次换板型/Lunch 都要重新选）：
cd /home
./build.sh lunch        # 选板型（如 RV1103_Luckfox_Pico_Plus）→ 启动媒介 → 系统版本
./build.sh             # 编译完整镜像
```

> 注意：所有 `docker` 命令需要 `sudo`（或把当前用户加入 docker 组），容器内编译不要再用 sudo。

## 一、怎么把模块源码放进 SDK（通用规则）

模块的 Makefile 用**相对路径**定位内核源码树：

```make
KERNEL_DIR=$(abspath $(CURDIR)/../../../source/kernel)
```

因此模块必须放进 SDK 树的**固定深度**才能编译：

```
<SDK>/sysdrv/drv_ko/my_drv/<模块名>/
        ↑ 从 <模块名> 往上数三层刚好是 sysdrv，下一级才是 source/kernel
```

> **全新克隆的 SDK 里没有 `my_drv/` 这个文件夹**（它是惯例上用户自建的"我的驱动"目录，SDK 仓库不包含它）。首次使用先自己创建：
>
> ```bash
> mkdir -p <SDK>/sysdrv/drv_ko/my_drv
> ```
>
> 只要保持"sysdrv 下三层深"这个相对深度，放在别的目录名（如 `sysdrv/drv_ko/随便起名/`）也能编译；`my_drv/` 只是官方社区约定的名字。

放置方式（二选一，先在 SDK 里建好目录）：

```bash
# 方式 A：直接拷贝（简单，但仓库和 SDK 各有一份，改代码记得同步）
cp -r rv1103_drv/drv/MD0240_LCD <SDK>/sysdrv/drv_ko/my_drv/

# 方式 B：软链接（推荐，仓库就是唯一源码，SDK 里开发无需手动同步）
ln -s /path/to/rv1103_drv/drv/MD0240_LCD <SDK>/sysdrv/drv_ko/my_drv/MD0240_LCD
```

> 编译时**从 SDK 树内的路径进入**（`cd <SDK>/sysdrv/drv_ko/my_drv/<模块>` 再 `make`），Makefile 的相对路径才能命中内核树。直接在仓库路径里 `make` 会报内核目录找不到。

## 二、编译

**前置条件**：

1. 完整 SDK（含内核源码 `sysdrv/source/kernel`），按官方教程拉取即可；
2. 交叉编译工具链 `arm-rockchip830-linux-uclibcgnueabihf-`：SDK 自带（`<SDK>/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin`），把 `bin` 目录加进 `PATH`。
   本仓库 Makefile 会依次探测 `PATH` 和 `~/Tools/` 下是否已有该工具链，找不到时请设置 PATH 后重试。

**编译**（宿主机直接编，无需 Docker）：

```bash
cd <SDK>/sysdrv/drv_ko/my_drv/<模块>
make
```

产物（以 MD0240_LCD 为例）：

```
ko/md0240_lcd.ko     ← 最终模块（拷去开发板的就它）
obj/                 ← 中间对象（.o、.mod、*.cmd 等，已 gitignore）
```

Makefile 已内置：`ARCH=arm`、`O=$(KERNEL_OUT)` 外部构建（不污染内核源码树）、跨编译器自动探测。

> 若内核对 `KERNEL_DIR` / `KERNEL_OUT` 与你本机 SDK 布局不同，可用环境方式覆盖：
> `make KERNEL_DIR=/your/sysdrv/source/kernel KERNEL_OUT=/your/sysdrv/source/objs_kernel`

**常见编译错误**：

| 现象 | 原因与解决 |
|---|---|
| `arm-rockchip830-linux-uclibcgnueabihf-gcc: not found` | 工具链 bin 不在 PATH，导出后再 make |
| `*** The source tree is not clean, please run 'make mrproper'` | 内核树有残留 `.config`，进 `sysdrv/source/kernel` 执行 `make ARCH=arm mrproper` 后重试 |
| `mkdir: Permission denied` | `objs_kernel` 归属容器 root：在容器里编译，或 `sudo chown -R $(id -un):$(id -gn) sysdrv/source/objs_kernel`（之后不用 sudo 编译） |
| 所有 make 均不要加 `sudo` | 容器内本身就是 root；宿主上 sudo 编译会留下 root 属主文件 |

## 三、清理

```bash
cd <SDK>/sysdrv/drv_ko/my_drv/<模块>
make clean
```

删除本模块的 `ko/`、`obj/` 内容，并清理 `KERNEL_OUT`（objs_kernel）中本模块的编译产物。不会动内核源码树。

## 四、导入开发板并使用（通用流程）

```bash
# ① 宿主机上传（USB 网络 / 网口，IP 见官方文档或板上 adb shell 查询）
scp ko/<模块名>.ko root@<开发板IP>:/root/

# ② 板子上加载
root@luckfox:~# insmod /root/<模块名>.ko

# ③ 验证（各模块的详细验证步骤见其 README.md）
root@luckfox:~# dmesg | grep <特征串>     # probe / 注册日志
root@luckfox:~# lsmod | grep <模块名>

# ④ 卸载
root@luckfox:~# rmmod <模块名>
```

**开机自启**（可选）：在板子 `/etc/init.d/` 添加脚本，例如 `S99<模块名>` 内：

```sh
insmod /root/<模块名>.ko
```

## 五、设备树改动后如何生效（涉及的模块通用）

需要改 dts 的模块（如 SPI/GPIO 外设），改动后的 dts 要"覆盖到内核树 → 容器里重编 boot.img → 只烧 boot 分区"才能生效：

```bash
# ① 宿主：把改好的 dts 覆盖到内核树（make 只编内核树里的文件）
cp /path/to/rv1103_drv/drv/<模块>/*-pico-plus.dts \
   <SDK>/sysdrv/source/kernel/arch/arm/boot/dts/

# ② 宿主：进入容器（boot.img 必须进 Docker 编）
sudo docker start -ai luckfox
# 容器内：
cd /home/sysdrv/source/kernel
make ARCH=arm CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf- \
  O=/home/sysdrv/source/objs_kernel \
  rv1103g-luckfox-pico-plus.img \
  BOOT_ITS=/home/sysdrv/source/kernel/boot.its -j4
cp /home/sysdrv/source/objs_kernel/boot.img /home/sysdrv/out/image_uclibc_rv1106/boot.img

# ③ Windows SocToolKit（管理员），板子进 loader（按住 BOOT 插 USB 或板内 reboot loader），
#    只勾选 boot 分区烧录；不会动 uboot/rootfs
```

完整细节（备份原版 dts、板上回读验证、回退、mkimage 验证、各报错解释）见 [docs/设备树编译与烧录指南.md](docs/设备树编译与烧录指南.md)。

