#include <linux/fb.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "md0240_lcd.h"

static ssize_t md0240_lcd_fb_write(struct fb_info *info, const char __user *buf,
				   size_t count, loff_t *ppos);
static int md0240_lcd_fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
				   unsigned int blue, unsigned int transp, struct fb_info *info);
static void md0240_lcd_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
static void md0240_lcd_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region);
static void md0240_lcd_fb_imageblit(struct fb_info *info, const struct fb_image *image);
static int md0240_lcd_fb_blank(int blank, struct fb_info *info);
static void md0240_lcd_fb_destroy(struct fb_info *info);

static struct fb_ops md0240_lcd_fb_ops = {
    .owner        = THIS_MODULE,
    .fb_read      = fb_sys_read,
    .fb_write     = md0240_lcd_fb_write,
    .fb_setcolreg = md0240_lcd_fb_setcolreg,
    .fb_fillrect  = md0240_lcd_fb_fillrect,
    .fb_copyarea  = md0240_lcd_fb_copyarea,
    .fb_imageblit = md0240_lcd_fb_imageblit,
    .fb_blank     = md0240_lcd_fb_blank,
    .fb_destroy   = md0240_lcd_fb_destroy,
};

/**
 * @brief 标记屏幕脏行范围，并调度延迟刷新
 *
 * @param info fb_info 指针
 * @param y 脏区起始行
 * @param height 脏区行数
 */
static void md0240_lcd_mkdirty(struct fb_info *info, u32 y, u32 height)
{
    struct md0240_lcd_dev *lcd = info->par;
    struct fb_deferred_io *fbdefio = info->fbdefio;

    spin_lock(&lcd->dirty_lock);
    if (y < lcd->dirty_lines_start) {
        lcd->dirty_lines_start = y;
    }
    if (y + height > lcd->dirty_lines_end) {
        lcd->dirty_lines_end = y + height;
    }
    spin_unlock(&lcd->dirty_lock);

    schedule_delayed_work(&info->deferred_work, fbdefio ? fbdefio->delay : HZ / 10);
}

/**
 * @brief deferred_io 回调：收集脏行范围并刷屏（延迟批量刷新）
 *
 * @param info fb_info 指针
 * @param pagelist mmap 缺页标脏的页列表（用户 mmap 写触发）
 */
static void md0240_lcd_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
    struct md0240_lcd_dev *lcd = info->par;
    struct page *page;
    unsigned long index;
    u32 y_low = 0, y_high = 0;
    u32 dirty_start, dirty_end;

    spin_lock(&lcd->dirty_lock);
    dirty_start = lcd->dirty_lines_start;
    dirty_end = lcd->dirty_lines_end;
    /* 取出后立即重置为"无脏行"状态 */
    lcd->dirty_lines_start = info->var.yres - 1;
    lcd->dirty_lines_end = 0;
    spin_unlock(&lcd->dirty_lock);

    /* 合并 mmap 缺页标脏的行范围（y_low/y_high 为闭区间行号，转排他上界） */
    list_for_each_entry(page, pagelist, lru) {
        index = page->index << PAGE_SHIFT;
        y_low = index / info->fix.line_length;
        y_high = (index + PAGE_SIZE - 1) / info->fix.line_length;
        if (y_high > info->var.yres - 1) {
            y_high = info->var.yres - 1;
        }
        if (y_low < dirty_start) {
            dirty_start = y_low;
        }
        if (y_high + 1 > dirty_end) {
            dirty_end = y_high + 1;
        }
    }

    if (dirty_end > info->var.yres) {   /* 防止越界多刷一行 */
        dirty_end = info->var.yres;
    }
    if (dirty_end <= dirty_start) {     /* 没有脏行 */
        return;
    }

    mutex_lock(&lcd->lock);
    md0240_lcd_blit(lcd, (const u16 *)lcd->vmem, dirty_start, dirty_end);
    mutex_unlock(&lcd->lock);
}

/**
 * @brief 用户 write() 写显存：拷入 vmem 并标脏
 *
 * @param info fb_info 指针
 * @param buf 用户空间数据
 * @param count 写入字节数
 * @param ppos 文件偏移
 * @return ssize_t 实际写入字节数
 */
static ssize_t md0240_lcd_fb_write(struct fb_info *info, const char __user *buf,
				   size_t count, loff_t *ppos)
{
    ssize_t res;

    /* 用户数据写入 vmem（fb_sys_write 处理越界、偏移、部分写） */
    res = fb_sys_write(info, buf, count, ppos);
    if (res > 0) {
        md0240_lcd_mkdirty(info, 0, info->var.yres);   /* 标脏，由 deferred_io 刷屏 */
    }

    return res;
}

/**
 * @brief 矩形填充：内核软绘制进 vmem 后标脏
 */
static void md0240_lcd_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
    sys_fillrect(info, rect);
    md0240_lcd_mkdirty(info, rect->dy, rect->height);
}

/**
 * @brief 区域拷贝：内核软绘制进 vmem 后标脏
 */
static void md0240_lcd_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
    sys_copyarea(info, region);
    md0240_lcd_mkdirty(info, region->dy, region->height);
}

/**
 * @brief 位图绘制（fbcon 文字）：内核软绘制进 vmem 后标脏
 */
static void md0240_lcd_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
    sys_imageblit(info, image);
    md0240_lcd_mkdirty(info, image->dy, image->height);
}

/**
 * @brief 设置颜色寄存器（16bpp 真彩填充伪调色板，fbcon 文字颜色用）
 */
static int md0240_lcd_fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green,
				   unsigned int blue, unsigned int transp, struct fb_info *info)
{
    u32 *pal = info->pseudo_palette;

    if (regno >= 16) {
        return 1;
    }

    red   >>= 16 - info->var.red.length;
    green >>= 16 - info->var.green.length;
    blue  >>= 16 - info->var.blue.length;

    pal[regno] = (red << info->var.red.offset) |
                 (green << info->var.green.offset) |
                 (blue << info->var.blue.offset);
    return 0;
}

/**
 * @brief 息屏/亮屏
 */
static int md0240_lcd_fb_blank(int blank, struct fb_info *info)
{
    struct md0240_lcd_dev *lcd = info->par;

    if (blank == FB_BLANK_UNBLANK) {
        md0240_lcd_write_cmd(lcd, 0x29);   /* 开显示 */
        gpiod_set_value(lcd->pwr, 1);      /* 背光亮 */
    } else {
        md0240_lcd_write_cmd(lcd, 0x28);   /* 关显示 */
        gpiod_set_value(lcd->pwr, 0);      /* 背光灭 */
    }
    return 0;
}

/**
 * @brief 释放 fb 资源（引用计数归零时由内核调用）
 */
static void md0240_lcd_fb_destroy(struct fb_info *info)
{
    struct md0240_lcd_dev *lcd = info->par;

    fb_deferred_io_cleanup(info);
    if (lcd) {
        vfree(lcd->vmem);
        lcd->vmem = NULL;
        lcd->fb = NULL;
    }
    framebuffer_release(info);
}

/**
 * @brief 初始化 framebuffer 并注册到内核
 *
 * @param md0240_lcd LCD 设备指针
 * @return int 0 成功，其他值失败
 */
int md0240_lcd_fb_init(struct md0240_lcd_dev *md0240_lcd)
{
    struct fb_info *info;
    struct fb_deferred_io *fbdefio;
    u32 vmem_size = MD0240_LCD_WIDTH * MD0240_LCD_HEIGHT * sizeof(u16);
    int ret;

    md0240_lcd->width = MD0240_LCD_WIDTH;
    md0240_lcd->height = MD0240_LCD_HEIGHT;

    md0240_lcd->vmem = vzalloc(vmem_size);
    if (!md0240_lcd->vmem) {
        return -ENOMEM;
    }

    info = framebuffer_alloc(0, md0240_lcd->dev);
    if (!info) {
        ret = -ENOMEM;
        goto free_vmem;
    }
    md0240_lcd->fb = info;

    info->par = md0240_lcd;
    info->fbops = &md0240_lcd_fb_ops;
    info->screen_base = md0240_lcd->vmem;
    info->screen_buffer = md0240_lcd->vmem;
    info->screen_size = vmem_size;
    info->pseudo_palette = md0240_lcd->pseudo_palette;
    info->flags = FBINFO_FLAG_DEFAULT;

    strcpy(info->fix.id, "md0240_lcd");
    info->fix.type = FB_TYPE_PACKED_PIXELS;
    info->fix.visual = FB_VISUAL_TRUECOLOR;
    info->fix.line_length = MD0240_LCD_WIDTH * sizeof(u16);
    info->fix.smem_len = vmem_size;
    info->fix.accel = FB_ACCEL_NONE;

    info->var.xres = MD0240_LCD_WIDTH;
    info->var.yres = MD0240_LCD_HEIGHT;
    info->var.xres_virtual = MD0240_LCD_WIDTH;
    info->var.yres_virtual = MD0240_LCD_HEIGHT;
    info->var.bits_per_pixel = MD0240_LCD_BPP;
    info->var.red.length = 5;   info->var.red.offset = 11;
    info->var.green.length = 6; info->var.green.offset = 5;
    info->var.blue.length = 5;  info->var.blue.offset = 0;
    info->var.activate = FB_ACTIVATE_NOW;

    /* deferred_io 刷新引擎：100ms 合并一次刷新 */
    fbdefio = devm_kzalloc(md0240_lcd->dev, sizeof(*fbdefio), GFP_KERNEL);
    if (!fbdefio) {
        ret = -ENOMEM;
        goto free_info;
    }
    fbdefio->delay = HZ / 60;   /* 16ms 合并刷新，打字跟手 */
    fbdefio->deferred_io = md0240_lcd_deferred_io;
    info->fbdefio = fbdefio;
    fb_deferred_io_init(info);

    ret = register_framebuffer(info);
    if (ret) {
        goto free_info;
    }

    return 0;

free_info:
    framebuffer_release(info);
    md0240_lcd->fb = NULL;
    vfree(md0240_lcd->vmem);
    md0240_lcd->vmem = NULL;
    return ret;

free_vmem:
    vfree(md0240_lcd->vmem);
    md0240_lcd->vmem = NULL;
    return ret;
}

/**
 * @brief 注销 framebuffer（fb_info 和 vmem 由 fb_destroy 在引用归零后释放）
 *
 * @param md0240_lcd LCD 设备指针
 */
void md0240_lcd_fb_exit(struct md0240_lcd_dev *md0240_lcd)
{
    if (md0240_lcd->fb) {
        unregister_framebuffer(md0240_lcd->fb);
        md0240_lcd->fb = NULL;
    }
}
