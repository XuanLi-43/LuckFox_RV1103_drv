#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include "md0240_lcd.h"

static const struct of_device_id md0240_lcd_of_match[] = {
    { .compatible = "atk_md0240,md0240_lcd" }, // 匹配 ATK MD0240 LCD 设备
    {/* 空结尾 */},
};
MODULE_DEVICE_TABLE(of, md0240_lcd_of_match); //生成模块别名

static int md0240_lcd_probe(struct spi_device *spi);
static int md0240_lcd_remove(struct spi_device *spi);
static struct spi_driver md0240_lcd_driver = {
    .probe = md0240_lcd_probe,
    .remove = md0240_lcd_remove,
    .driver = {
        .name = "md0240_lcd",
        .of_match_table = md0240_lcd_of_match,
    },
};

/**
 * @brief 匹配 MD0240 LCD 设备
 * 
 * @param spi SPI 设备指针
 * @return int 0 成功，其他值失败
 */
static int md0240_lcd_probe(struct spi_device *spi)
{
    int ret;
    struct md0240_lcd_dev* md0240_lcd;

    dev_info(&spi->dev, "md0240_lcd_probe start\n");

    md0240_lcd = devm_kzalloc(&spi->dev, sizeof(*md0240_lcd), GFP_KERNEL);
    if(!md0240_lcd)
    {
        dev_err(&spi->dev, "md0240_lcd alloc fail\n");
        goto alloc_fail;
    }

    md0240_lcd->spi = spi;
    md0240_lcd->dev = &spi->dev;
    spi_set_drvdata(spi, md0240_lcd); // 存储设备私有数据

    mutex_init(&md0240_lcd->lock); // 初始化互斥锁

    md0240_lcd->buf = devm_kzalloc(&spi->dev, MD0240_LCD_BUF_SIZE, GFP_KERNEL);
    if(!md0240_lcd->buf)
    {
        dev_err(&spi->dev, "alloc lcd tx buf fail\n");
        goto alloc_fail;
    }

    md0240_lcd->dc = devm_gpiod_get(&spi->dev, "dc", GPIOD_OUT_LOW);
    if(IS_ERR(md0240_lcd->dc))
    {
        ret = PTR_ERR(md0240_lcd->dc);
        dev_err(&spi->dev, "get dc gpio fail: %d\n", ret);
        goto fail;
    }

    md0240_lcd->rst = devm_gpiod_get(&spi->dev, "rst", GPIOD_OUT_LOW);
    if(IS_ERR(md0240_lcd->rst))
    {
        ret = PTR_ERR(md0240_lcd->rst);
        dev_err(&spi->dev, "get rst gpio fail: %d\n", ret);
        goto fail;
    }

    md0240_lcd->pwr = devm_gpiod_get(&spi->dev, "pwr", GPIOD_OUT_LOW);
    if(IS_ERR(md0240_lcd->pwr))
    {
        ret = PTR_ERR(md0240_lcd->pwr);
        dev_err(&spi->dev, "get pwr gpio fail: %d\n", ret);
        goto fail;
    }

    // 配置 SPI 模式
    spi->mode = SPI_MODE_0;        // CPOL=0, CPHA=0
    spi->max_speed_hz = 40000000;  // 40MHz（ST7789 标称支持 62.5MHz；花屏就降到 30MHz）
    ret = spi_setup(spi);          // 调用 spi_setup 让配置生效
    if(ret)
    {
        dev_err(&spi->dev, "spi_setup fail\n");
        goto fail;
    }

    //初始化spi
    ret = md0240_lcd_init(md0240_lcd);
    if(ret)
    {
        dev_err(&spi->dev, "md0240_lcd_init fail\n");
        goto fail; 
    }

    //初始化fb
    ret = md0240_lcd_fb_init(md0240_lcd);
    if(ret)
    {
        dev_err(&spi->dev, "md0240_lcd_fb_init fail\n");
        goto fail; 
    }

    dev_info(&spi->dev, "md0240_lcd_fb_init registered, /dev/fb%d\n", md0240_lcd->fb->node);
    return 0;

alloc_fail:
    return -ENOMEM;

fail:
    return ret;
}

/**
 * @brief 移除 MD0240 LCD 设备
 * 
 * @param spi SPI 设备指针
 */
static int md0240_lcd_remove(struct spi_device *spi)
{
    struct md0240_lcd_dev* md0240_lcd = spi_get_drvdata(spi); // 获取设备私有数据
    dev_info(&spi->dev, "md0240_lcd_remove start\n");
    md0240_lcd_fb_exit(md0240_lcd);
    return 0;
}

module_spi_driver(md0240_lcd_driver);

MODULE_LICENSE("GPL");
