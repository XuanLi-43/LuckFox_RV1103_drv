#include <linux/delay.h>
#include "md0240_lcd.h"

/* rockchip-spi 单次传输上限（0xffff 字节），大块数据必须分块发送 */
#define MD0240_SPI_MAX_XFER 0xffff

/**
 * @brief 分块发送 SPI 数据（超过控制器单次传输上限时自动分块）
 *
 * @param md0240_lcd LCD 设备指针
 * @param data 发送数据
 * @param len 数据长度（字节）
 */
static void md0240_lcd_spi_send(struct md0240_lcd_dev *md0240_lcd, const u8 *data, u32 len)
{
    while (len > 0) {
        u32 chunk = min(len, (u32)MD0240_SPI_MAX_XFER);
        spi_write(md0240_lcd->spi, data, chunk);
        data += chunk;
        len -= chunk;
    }
}

/**
 * @brief 控制背光
 * 
 * @param md0240_lcd LCD 设备指针
 * @param x 0: 关闭, 1: 开启
 */
static inline void md0240_lcd_pwr_io(struct md0240_lcd_dev *md0240_lcd, int x)
{
    gpiod_set_value(md0240_lcd->pwr, x);
}

/**
 * @brief 控制数据/命令引脚
 * 
 * @param md0240_lcd LCD 设备指针
 * @param x 0: 命令, 1: 数据
 */
static inline void md0240_lcd_dc_io(struct md0240_lcd_dev *md0240_lcd, int x)
{
    gpiod_set_value(md0240_lcd->dc, x);
}

/**
 * @brief 控制复位引脚
 *
 * @param md0240_lcd LCD 设备指针
 * @param x 逻辑电平：1=复位生效，0=释放复位（物理极性由 dts 决定）
 */
static inline void md0240_lcd_rst_io(struct md0240_lcd_dev *md0240_lcd, int x)
{
    gpiod_set_value(md0240_lcd->rst, x);
}

/**
 * @brief 写入命令
 * 
 * @param md0240_lcd LCD 设备指针
 * @param cmd 命令字节
 */
void md0240_lcd_write_cmd(struct md0240_lcd_dev *md0240_lcd, u8 cmd)
{
    md0240_lcd_dc_io(md0240_lcd, 0);
    spi_write(md0240_lcd->spi, &cmd, sizeof(cmd));
}

/**
 * @brief 写入单个字节数据
 *
 * @param md0240_lcd LCD 设备指针
 * @param data 数据
 */
void md0240_lcd_write_data(struct md0240_lcd_dev *md0240_lcd, u8 data)
{
    md0240_lcd_dc_io(md0240_lcd, 1);
    spi_write(md0240_lcd->spi, &data, sizeof(data));
}

/**
 * @brief 复位 LCD
 * 
 * @param md0240_lcd LCD 设备指针
 */
static void md0240_lcd_rst(struct md0240_lcd_dev *md0240_lcd)
{
    md0240_lcd_rst_io(md0240_lcd, 1);   /* 复位生效（逻辑1=有效，dts ACTIVE_LOW → 物理拉低） */
    usleep_range(10000, 20000);         /* 保持复位 10ms */
    md0240_lcd_rst_io(md0240_lcd, 0);   /* 释放复位 */
    msleep(120);                        /* 等屏内部上电初始化完成 */
}

/**
 * @brief 开启 LCD 背光
 */
static void md0240_lcd_display_on(struct md0240_lcd_dev *md0240_lcd)
{
    md0240_lcd_pwr_io(md0240_lcd, 1);
}

static void md0240_lcd_reg_init(struct md0240_lcd_dev *md0240_lcd)
{
    /* 退出睡眠模式 */
    md0240_lcd_write_cmd(md0240_lcd, 0x11);
    msleep(120);
    /* 内存数据访问控制 */
    md0240_lcd_write_cmd(md0240_lcd, 0x36);

    md0240_lcd_write_data(md0240_lcd, 0x00);   /* 竖屏 240x320（默认扫描方向） */

    /* RGB 5-6-5 位色 */
    md0240_lcd_write_cmd(md0240_lcd,0x3A);
    md0240_lcd_write_data(md0240_lcd, 0x65);
    /* 前后肩设置 */
    md0240_lcd_write_cmd(md0240_lcd,0xB2);
    md0240_lcd_write_data(md0240_lcd, 0x0C);
    md0240_lcd_write_data(md0240_lcd, 0x0C);
    md0240_lcd_write_data(md0240_lcd, 0x00);
    md0240_lcd_write_data(md0240_lcd, 0x33);
    md0240_lcd_write_data(md0240_lcd, 0x33);
    /* 栅极控制 */
    md0240_lcd_write_cmd(md0240_lcd,0xB7);
    md0240_lcd_write_data(md0240_lcd, 0x72);
    /* VCOM 设置 */
    md0240_lcd_write_cmd(md0240_lcd,0xBB);
    md0240_lcd_write_data(md0240_lcd, 0x3D);
    /* LCM 控制 */
    md0240_lcd_write_cmd(md0240_lcd,0xC0);
    md0240_lcd_write_data(md0240_lcd, 0x2C);
    /* VDV 和 VRH 命令使能 */
    md0240_lcd_write_cmd(md0240_lcd,0xC2);
    md0240_lcd_write_data(md0240_lcd, 0x01);
    /* VRH 设置 */
    md0240_lcd_write_cmd(md0240_lcd,0xC3);
    md0240_lcd_write_data(md0240_lcd, 0x19);
    /* VDV 设置 */
    md0240_lcd_write_cmd(md0240_lcd,0xC4);
    md0240_lcd_write_data(md0240_lcd, 0x20);
    /* 正常模式帧率控制 */
    md0240_lcd_write_cmd(md0240_lcd,0xC6);
    md0240_lcd_write_data(md0240_lcd, 0x0F);
    /* 电源控制 1 */
    md0240_lcd_write_cmd(md0240_lcd,0xD0);
    md0240_lcd_write_data(md0240_lcd, 0xA4);
    md0240_lcd_write_data(md0240_lcd, 0xA1);
    /* 正电压 Gamma 控制 */
    md0240_lcd_write_cmd(md0240_lcd,0xE0);
    md0240_lcd_write_data(md0240_lcd, 0xD0);
    md0240_lcd_write_data(md0240_lcd, 0x04);
    md0240_lcd_write_data(md0240_lcd, 0x0D);
    md0240_lcd_write_data(md0240_lcd, 0x11);
    md0240_lcd_write_data(md0240_lcd, 0x24);
    md0240_lcd_write_data(md0240_lcd, 0x2B);
    md0240_lcd_write_data(md0240_lcd, 0x3F);
    md0240_lcd_write_data(md0240_lcd, 0x54);
    md0240_lcd_write_data(md0240_lcd, 0x4C);
    md0240_lcd_write_data(md0240_lcd, 0x18);
    md0240_lcd_write_data(md0240_lcd, 0x0D);
    md0240_lcd_write_data(md0240_lcd, 0x0B);
    md0240_lcd_write_data(md0240_lcd, 0x1F);
    md0240_lcd_write_data(md0240_lcd, 0x23);
    /* 负电压 Gamma 控制 */
    md0240_lcd_write_cmd(md0240_lcd,0xE1);
    md0240_lcd_write_data(md0240_lcd, 0xD0);
    md0240_lcd_write_data(md0240_lcd, 0x04);
    md0240_lcd_write_data(md0240_lcd, 0x0C);
    md0240_lcd_write_data(md0240_lcd, 0x11);
    md0240_lcd_write_data(md0240_lcd, 0x24);
    md0240_lcd_write_data(md0240_lcd, 0x2C);
    md0240_lcd_write_data(md0240_lcd, 0x3F);
    md0240_lcd_write_data(md0240_lcd, 0x44);
    md0240_lcd_write_data(md0240_lcd, 0x51);
    md0240_lcd_write_data(md0240_lcd, 0x2F);
    md0240_lcd_write_data(md0240_lcd, 0x1F);
    md0240_lcd_write_data(md0240_lcd, 0x1F);
    md0240_lcd_write_data(md0240_lcd, 0x20);
    md0240_lcd_write_data(md0240_lcd, 0x23);
    /* 显示反转开启 */
    md0240_lcd_write_cmd(md0240_lcd,0x21);
    md0240_lcd_write_cmd(md0240_lcd,0x29);
}

/**
 * @brief 设置 LCD 行列地址
 * 
 * @param md0240_lcd LCD 设备指针
 * @param xs 起始列地址
 * @param ys 起始行地址
 * @param xe 结束列地址
 * @param ye 结束行地址
 */
static void md0240_lcd_set_xy_addr(struct md0240_lcd_dev *md0240_lcd, u16 xs, u16 ys, u16 xe, u16 ye)
{
    md0240_lcd_write_cmd(md0240_lcd, 0x2A);
    md0240_lcd_write_data(md0240_lcd, (u8)(xs >> 8));
    md0240_lcd_write_data(md0240_lcd, (u8)xs);
    md0240_lcd_write_data(md0240_lcd, (u8)(xe >> 8));
    md0240_lcd_write_data(md0240_lcd, (u8)xe);
    md0240_lcd_write_cmd(md0240_lcd, 0x2B);
    md0240_lcd_write_data(md0240_lcd, (u8)(ys >> 8));
    md0240_lcd_write_data(md0240_lcd, (u8)ys);
    md0240_lcd_write_data(md0240_lcd, (u8)(ye >> 8));
    md0240_lcd_write_data(md0240_lcd, (u8)ye);
    md0240_lcd_write_cmd(md0240_lcd, 0x2C);
}

/**
 * @brief  MD0240 模块 LCD 区域填充
 *
 * @param md0240_lcd LCD 设备指针
 * @param xs 区域起始 X 坐标
 * @param ys 区域起始 Y 坐标
 * @param xe 区域终止 X 坐标
 * @param ye 区域终止 Y 坐标
 * @param color 填充颜色（RGB565）
 */
void md0240_lcd_fill(struct md0240_lcd_dev *md0240_lcd, u16 xs, u16 ys, u16 xe, u16 ye, u16 color)
{
    u32 bytes = (xe - xs + 1) * (ye - ys + 1) * sizeof(u16);
    u8 *dst = md0240_lcd->buf;
    u32 i;

    /* 构建 RGB565 高字节先行的单色像素流 */
    for (i = 0; i < bytes; i += 2) {
        dst[i]     = (u8)(color >> 8);
        dst[i + 1] = (u8)color;
    }

    md0240_lcd_set_xy_addr(md0240_lcd, xs, ys, xe, ye);
    md0240_lcd_dc_io(md0240_lcd, 1);
    md0240_lcd_spi_send(md0240_lcd, dst, bytes);
}

/**
 * @brief  MD0240 模块 LCD 清屏
 *
 * @param md0240_lcd LCD 设备指针
 * @param color 清屏填充颜色（RGB565）
 */
void md0240_lcd_clear(struct md0240_lcd_dev *md0240_lcd, u16 color)
{
    md0240_lcd_fill(md0240_lcd, 0, 0, MD0240_LCD_WIDTH - 1, MD0240_LCD_HEIGHT - 1, color);
}

/**
 * @brief  MD0240 模块向指定矩形区域写入 RGB565 像素数据
 *
 * @param md0240_lcd LCD 设备指针
 * @param xs 区域起始 X 坐标
 * @param ys 区域起始 Y 坐标
 * @param width 区域宽度
 * @param height 区域高度
 * @param img RGB565 像素数组（内存中低字节在前，发送时交换为高字节先行）
 */
void md0240_lcd_write_pixels(struct md0240_lcd_dev *md0240_lcd, u16 xs, u16 ys,
                             u16 width, u16 height, const u16 *img)
{
    u32 pixels = (u32)width * height;
    u16 *dst = (u16 *)md0240_lcd->buf;
    u32 i;

    /* RGB565 低字节在前，发送前交换为高字节先行 */
    for (i = 0; i < pixels; i++)
        dst[i] = (u16)((img[i] >> 8) | (img[i] << 8));

    md0240_lcd_set_xy_addr(md0240_lcd, xs, ys, xs + width - 1, ys + height - 1);
    md0240_lcd_dc_io(md0240_lcd, 1);
    md0240_lcd_spi_send(md0240_lcd, (u8 *)dst, pixels * sizeof(u16));
}

/**
 * @brief 初始化 MD0240 LCD 设备
 * 
 * @param md0240_lcd LCD 设备指针
 * @return 0 成功
 */
int md0240_lcd_init(struct md0240_lcd_dev *md0240_lcd)
{
    md0240_lcd_rst(md0240_lcd);
    md0240_lcd_reg_init(md0240_lcd); 
    md0240_lcd_set_xy_addr(md0240_lcd, 0, 0, MD0240_LCD_WIDTH - 1, MD0240_LCD_HEIGHT - 1);
    md0240_lcd_display_on(md0240_lcd);
    md0240_lcd_clear(md0240_lcd, MD0240_LCD_WHITE);

    dev_info(md0240_lcd->dev, "md0240_lcd_init success\n");
    return 0;
}

/**
 * @brief 将显存指定行范围刷到屏幕（RGB565 高字节先行）
 *
 * @param md0240_lcd LCD 设备指针
 * @param vmem 显存（内存序，低字节在前）
 * @param y_start 起始行
 * @param y_end 终止行（不含）
 */
void md0240_lcd_blit(struct md0240_lcd_dev *md0240_lcd, const u16 *vmem, u16 y_start, u16 y_end)
{
    u32 pixels = (u32)(y_end - y_start) * MD0240_LCD_WIDTH;
    const u16 *src = vmem + (u32)y_start * MD0240_LCD_WIDTH;
    u16 *dst = (u16 *)md0240_lcd->buf;
    u32 i;

    /* RGB565 低字节在前，发送前交换为高字节先行 */
    for (i = 0; i < pixels; i++)
        dst[i] = (u16)((src[i] >> 8) | (src[i] << 8));

    md0240_lcd_set_xy_addr(md0240_lcd, 0, y_start, MD0240_LCD_WIDTH - 1, y_end - 1);
    md0240_lcd_dc_io(md0240_lcd, 1);
    md0240_lcd_spi_send(md0240_lcd, (u8 *)dst, pixels * sizeof(u16));
}