#ifndef __MD0240_LCD_H__
#define __MD0240_LCD_H__

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#define MD0240_LCD_NAME    "md0240_lcd"
#define MD0240_LCD_WIDTH   240   // 竖屏：宽 240
#define MD0240_LCD_HEIGHT  320   // 竖屏：高 320
#define MD0240_LCD_BPP     16   // RGB565
#define MD0240_LCD_BUF_SIZE (MD0240_LCD_WIDTH * MD0240_LCD_HEIGHT * 2) // 像素发送缓冲（字节）

/* 常用颜色定义（RGB565） */
#define MD0240_LCD_WHITE        0xFFFF
#define MD0240_LCD_BLACK        0x0000
#define MD0240_LCD_BLUE         0x001F
#define MD0240_LCD_BRED         0XF81F
#define MD0240_LCD_GRED         0XFFE0
#define MD0240_LCD_GBLUE        0X07FF
#define MD0240_LCD_RED          0xF800
#define MD0240_LCD_MAGENTA      0xF81F
#define MD0240_LCD_GREEN        0x07E0
#define MD0240_LCD_CYAN         0x7FFF
#define MD0240_LCD_YELLOW       0xFFE0
#define MD0240_LCD_BROWN        0XBC40
#define MD0240_LCD_BRRED        0XFC07
#define MD0240_LCD_GRAY         0X8430

struct md0240_lcd_dev 
{
    struct device *dev;             // 设备结构体（用于 devm_ 资源管理）
    struct spi_device *spi;         // SPI 设备指针（内核传入）

    struct gpio_desc *dc;           // 数据/命令 GPIO 描述符
    struct gpio_desc *rst;          // 复位 GPIO 描述符
    struct gpio_desc *pwr;           // 背光 GPIO 描述符

    struct fb_info *fb;             // Framebuffer 信息结构体
    u8 *vmem;                       // 显存虚拟地址（用于帧缓冲）
    u8 *buf;                        // 像素发送缓冲（RGB565 字节序交换用）
    u16 width, height;              // 屏幕分辨率
    u32 pseudo_palette[16];         // 伪调色板（RGB565 用
    
    struct mutex lock;              // 互斥锁：保护显存和刷新过程（防止并发写屏）
    int dirty_lines_start;          // 脏行范围起始（deferred_io 刷新用）
    int dirty_lines_end;            // 脏行范围结束
    spinlock_t dirty_lock;          // 保护脏行范围（软中断上下文访问）
};

int md0240_lcd_init(struct md0240_lcd_dev *md0240_lcd);
void md0240_lcd_write_cmd(struct md0240_lcd_dev *md0240_lcd, u8 cmd);
void md0240_lcd_write_data(struct md0240_lcd_dev *md0240_lcd, u8 data);
void md0240_lcd_fill(struct md0240_lcd_dev *md0240_lcd, u16 xs, u16 ys, u16 xe, u16 ye, u16 color);
void md0240_lcd_clear(struct md0240_lcd_dev *md0240_lcd, u16 color);
void md0240_lcd_write_pixels(struct md0240_lcd_dev *md0240_lcd, u16 xs, u16 ys,
                             u16 width, u16 height, const u16 *img);
void md0240_lcd_blit(struct md0240_lcd_dev *md0240_lcd, const u16 *vmem, u16 y_start, u16 y_end);

int md0240_lcd_fb_init(struct md0240_lcd_dev *md0240_lcd);
void md0240_lcd_fb_exit(struct md0240_lcd_dev *md0240_lcd);

#endif /* __MD0240_LCD_H__ */


