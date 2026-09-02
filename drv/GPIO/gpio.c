#include <linux/init.h>         
#include <linux/module.h>         
#include <linux/platform_device.h> 
#include <linux/gpio/consumer.h> 
#include <linux/of.h>              
#include <linux/fs.h>             
#include <linux/cdev.h>            
#include <linux/device.h>         
#include <linux/slab.h>           
#include <linux/uaccess.h>
#include <linux/kernel.h>            
#include <linux/pinctrl/consumer.h> 
#include <linux/string.h>         

#define GPIO_DEV_NAME    "gpio_dev"
#define GPIO_DEV_CNT      (1)

/* 全局变量定义*/
static dev_t        g_gpio_devno;    
static struct cdev  g_gpio_cdev;     
static struct class *g_gpio_class;   
static struct gpio_dev* g_gpio_data;

/* 硬件层私有数据结构体 */
struct gpio_dev {
    struct gpio_descs   *descs;
    struct pinctrl      *pinctrl;
    const char*         *names;       /* 硬件编号表，来自 dts 的 gpio-names */
    int                  nnames;      /* 名称数量 */
};

/* 文件层私有数据结构体 */
struct gpio_file_data
{
    struct gpio_dev* gpio_dev;
    int          current_pin;           /* 记录当前进程选择了哪个引脚下标 */
    char         current_pin_name[16];  /* 记录当前引脚名 */
    char         last_command[64];      /* 记录该进程输入的最后一条命令 */
};

/* 设备树匹配表 */
static const struct of_device_id gpio_of_match[] =  
{
    {.compatible = "luckfox-pico,gpio_dev"},
    {/*空结束*/}
};
MODULE_DEVICE_TABLE(of, gpio_of_match);

/* 函数前置声明 */
static int gpio_probe(struct platform_device *pdev);
static int gpio_remove(struct platform_device *pdev);
static int gpio_open(struct inode *inode, struct file *filep);
static int gpio_release(struct inode *inode, struct file *filep);
static ssize_t gpio_write(struct file *filep, const char __user *buf, size_t count, loff_t *ppos);
static ssize_t gpio_read(struct file *filep, char __user *buf, size_t count, loff_t *ppos);

/* 文件操作结构体 */
static const struct file_operations gpio_fops = {
    .owner   = THIS_MODULE,  
    .open    = gpio_open,
    .release = gpio_release,
    .write   = gpio_write,
    .read    = gpio_read,
};

/*----------------------------------------------Probe------------------------------------------------------*/
static int gpio_probe(struct platform_device *pdev)
{
    int ret = 0;
    int count, i;
    struct gpio_dev *data;
    struct device_node *np = pdev->dev.of_node;

    pr_info("gpio probe start\n");

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if(!data)
    {
        pr_err("gpio kzalloc fail\n");
        ret = -ENOMEM;
        goto err_out;
    }

    count = of_property_count_strings(np, "gpio-names");    /* 寻找设备树gpio-names节点 */
    if (count < 0) 
    {
        dev_err(&pdev->dev, "no gpio-names found in dts\n");
        ret = -EINVAL;
        goto err_out;
    }
    data->nnames = count;

    data->names = devm_kzalloc(&pdev->dev, sizeof(char *) * count, GFP_KERNEL);
    if(!data->names)
    {
        dev_err(&pdev->dev, "gpio-names kzalloc fail\n");
        ret = -ENOMEM;
        goto err_out;
    }

    for (i = 0; i < count; i++) 
    {
        if (of_property_read_string_index(np, "gpio-names", i, &data->names[i])) 
        {
            dev_err(&pdev->dev, "failed to read gpio-names[%d]\n", i);
            ret = -EINVAL;
            goto err_out;
        }
    }

    data->descs = devm_gpiod_get_array(&pdev->dev, NULL, GPIOD_ASIS);
    if (IS_ERR(data->descs))
    { 
        ret = PTR_ERR(data->descs);
        goto err_out;
    }

    data->pinctrl = devm_pinctrl_get(&pdev->dev);
    if (IS_ERR(data->pinctrl)) 
    {
        dev_warn(&pdev->dev, "no pinctrl available\n");
        data->pinctrl = NULL; /* 没有就不支持上下拉，但设备可能还能用 */
    }

    cdev_init(&g_gpio_cdev, &gpio_fops);
    ret = cdev_add(&g_gpio_cdev, g_gpio_devno, GPIO_DEV_CNT);
    if (ret < 0) 
    {
        dev_err(&pdev->dev, "gpio cdev_add failed: %d\n", ret);
        goto free_out;                 
    }

    g_gpio_class = class_create(THIS_MODULE, GPIO_DEV_NAME);
    if (IS_ERR(g_gpio_class)) 
    {
        ret = PTR_ERR(g_gpio_class);
        dev_err(&pdev->dev, "class_create failed: %d\n", ret);
        goto free_out;                 
    }
    device_create(g_gpio_class, NULL, g_gpio_devno, NULL, GPIO_DEV_NAME);

    /* 保存全局指针供 open 函数使用 */
    g_gpio_data = data;
    platform_set_drvdata(pdev, data);

    return 0;

err_out:
    return ret;

free_out:
    cdev_del(&g_gpio_cdev);
    return ret;
}

static int gpio_remove(struct platform_device *pdev)
{
    pr_info("gpio remove\n");

    device_destroy(g_gpio_class, g_gpio_devno);
    class_destroy(g_gpio_class);
    cdev_del(&g_gpio_cdev);
    
    g_gpio_data = NULL;   
    return 0;
}

/*----------------------------------------------fops------------------------------------------------------*/
static int gpio_open(struct inode *inode, struct file *filep)
{
    int ret;
    struct gpio_file_data *fdata;

    pr_info("gpio open\n");

    if(!g_gpio_data) 
    {
        pr_err("gpio not probed, no data\n");
        ret = -ENODEV;
        goto err_out;
    }

    if (!try_module_get(THIS_MODULE)) 
    {
        ret = -ENODEV;
        goto err_out;
    }

    fdata = kzalloc(sizeof(*fdata), GFP_KERNEL);
    if (!fdata) 
    {
        module_put(THIS_MODULE);
        ret = -ENOMEM;
        goto err_out;
    }

    fdata->gpio_dev = g_gpio_data;
    fdata->current_pin = -1; 

    filep->private_data = fdata;  
    return 0;

err_out:
    return ret;
}

static int gpio_release(struct inode *inode, struct file *filep)
{
    struct gpio_file_data *fdata = filep->private_data;
    
    pr_info("gpio release\n");

    if (fdata) 
    {
        kfree(fdata); /* 释放该进程独有的内存，防止泄漏 */
    }
    filep->private_data = NULL;

    module_put(THIS_MODULE);
    return 0;
}

/*----------------------------------------------Write------------------------------------------------------*/

/**
 * @brief 控制 GPIO 引脚的方向、上下拉模式及输出电平
 * 
 * 通过向设备节点写入文本协议来配置指定的 GPIO 引脚。
 * 
 * @return 成功时返回写入的字节数（count）；失败时返回负的错误码
 * 
 * @note  指令格式（空格分隔）:
 *        <pin> <dir> <mode> <val>
 *        - pin  : GPIO 硬件编号，如 "GPIO3_C6"（必须与 dts 的 gpio-names 一致）
 *        - dir  : "out"（输出）或 "in"（输入）
 *        - mode : "none"（无上下拉）、"up"（上拉）、"down"（下拉）
 *        - val  : 0 或 1（仅 dir 为 out 时有效，in 时忽略）
 * 
 * @note  示例:
 *        echo "GPIO3_C6 out none 1" > /dev/gpio_dev   // 输出高电平
 *        echo "GPIO3_C6 in up 0" > /dev/gpio_dev      // 设为输入，上拉，并记住此引脚供 read 使用
 * 
 * @warning 未经过合法参数校验时，可能会导致内核异常或引脚配置错误
 */
static ssize_t gpio_write(struct file *filep, const char __user *buf, size_t count, loff_t *ppos)
{
    struct gpio_file_data *fdata = filep->private_data;
    struct gpio_dev *data;
    struct gpio_desc *desc;
    struct pinctrl_state *state;
    char kbuf[64], pinstr[16], dir[8], mode[8], valstr[16], stname[16];
    unsigned long val;
    int i, nr, pin;
    ssize_t ret = count; 

    if (!fdata) 
    { 
        ret = -EIO; 
        goto err_out; 
    }
    
    /* 从 fdata 取出 gpio_dev 局部变量 */
    data = fdata->gpio_dev;
    if (!data) { 
        ret = -EIO; 
        goto err_out; 
    }

    if (!data->descs || IS_ERR(data->descs)) 
    { 
        ret = -EIO; 
        goto err_out; 
    }

    if (count < 1) 
    {
        ret = -EINVAL;
        goto err_out;
    }

    if (count >= sizeof(kbuf)) 
    {
        count = sizeof(kbuf) - 1;   /* 防止栈溢出 */
    }  

    if (copy_from_user(kbuf, buf, count)) 
    {
        ret = -EFAULT;
        goto err_out;
    }
    
    kbuf[count] = '\0';

    nr = sscanf(kbuf, "%15s %7s %7s %15s", pinstr, dir, mode, valstr);
    if (nr != 4) 
    {
        pr_err("format: <pin> <dir> <mode> <val>\n"); 
        ret = -EINVAL;
        goto err_out;
    }

    if (!data->names) 
    {
        pr_err("dts need gpio-names\n"); 
        ret = -EINVAL;
        goto err_out;
    }
    
    for (i = 0; i < data->nnames; i++) 
    {
        if (!strcmp(pinstr, data->names[i])) break;
    }

    if (i == data->nnames) 
    {
        pr_err("unknown pin '%s'\n", pinstr);
        ret = -EINVAL;
        goto err_out;
    }
    pin = i;

    if (kstrtoul(valstr, 10, &val) || val > 1) 
    {
        pr_err("bad val '%s' (0|1)\n", valstr);
        ret = -EINVAL;
        goto err_out;
    }

    desc = data->descs->desc[pin];

    if (strcmp(mode, "none") && strcmp(mode, "up") && strcmp(mode, "down")) 
    {
        pr_err("bad mode '%s' (none|up|down)\n", mode);
        ret = -EINVAL;
        goto err_out;
    }

    if (!data->pinctrl)
    {                       
        if (strcmp(mode, "none")) 
        {
            pr_err("mode '%s' unsupported: no pinctrl in dts node\n", mode);
            ret = -ENOTSUPP;            
            goto err_out;
        }
    } 
    else if (strcmp(mode, "none")) 
    {
        snprintf(stname, sizeof(stname), "%s-%s", pinstr, mode);
        state = pinctrl_lookup_state(data->pinctrl, stname);
        if (IS_ERR(state)) 
        {
            pr_err("pinctrl state '%s' not declared in dts\n", stname);
            ret = -ENOTSUPP;
            goto err_out;
        }
        nr = pinctrl_select_state(data->pinctrl, state);
        if (nr) 
        { 
            pr_err("pinctrl_select_state failed: %d\n", nr); 
            ret = nr; 
            goto err_out; 
        }
    }

    if (!strcmp(dir, "out")) 
    {
        gpiod_direction_output(desc, val);      
    } 
    else if (!strcmp(dir, "in")) 
    {
        gpiod_direction_input(desc);   
        /* 记录当前进程要读取的引脚 */
        fdata->current_pin = pin;
        strncpy(fdata->current_pin_name, pinstr, sizeof(fdata->current_pin_name) - 1);
    } 
    else 
    {
        pr_err("bad dir '%s' (out|in)\n", dir);
        ret = -EINVAL;
        goto err_out;
    }

    /* 记录该进程的最近一次命令 */
    strncpy(fdata->last_command, kbuf, sizeof(fdata->last_command) - 1);

    pr_info("gpio[%d] dir:%s mode:%s val:%lu\n", pin, dir, mode, val);

err_out:
    return ret;                              
}

/**
 * @brief 读取指定 GPIO 引脚的电平状态
 * 
 * 必须先通过 write 将目标引脚设置为输入模式（dir="in"），
 * 驱动会记录该引脚。read 会返回该引脚的当前电平以及最近一次的命令。
 * 
 * @return 成功时返回实际读取的字节数；失败时返回负的错误码
 * 
 * @note  用法示例:
 *        1. 先在终端设置输入模式：
 *           echo "GPIO3_C6 in none 0" > /dev/gpio_dev
 *        2. 再读取状态：
 *           cat /dev/gpio_dev
 *           输出格式: "PIN: GPIO3_C6, Level: 0\nLast Cmd: GPIO3_C6 in none 0\n"
 * 
 * @warning 如果未通过 write 设置过输入引脚，read 将返回 -EINVAL
 */
static ssize_t gpio_read(struct file *filep, char __user *buf, size_t count, loff_t *ppos)
{
    struct gpio_file_data *fdata = filep->private_data;
    struct gpio_dev *data;
    struct gpio_desc *desc;
    char kbuf[64];
    int value;
    int len;
    ssize_t ret = 0; /* 默认返回成功长度 */

    if (!fdata) 
    { 
        ret = -EIO; 
        goto err_out; 
    }

    data = fdata->gpio_dev;
    if (!data) 
    { 
        ret = -EIO; 
        goto err_out; 
    }

    /* 处理只读一次的逻辑（防止 cat 死循环） */
    if (ppos && *ppos > 0) 
    {
        return 0;
    }

    /* 检查是否已经设置了要读取的引脚 */
    if (fdata->current_pin < 0 || fdata->current_pin >= data->nnames) 
    {
        pr_err("no pin set for this file. write: echo \"PIN in none 0\" > /dev/gpio_dev\n");
        ret = -EINVAL;
        goto err_out;
    }

    desc = data->descs->desc[fdata->current_pin];
    value = gpiod_get_value(desc); /* 读取电平 0 或 1 */

    len = snprintf(kbuf, sizeof(kbuf), "PIN: %s, Level: %d\nLast Cmd: %s\n", 
                    fdata->current_pin_name, value, fdata->last_command);

    if (len > count) 
    {
        len = count;
    }

    if (copy_to_user(buf, kbuf, len)) 
    { 
        ret = -EFAULT; 
        goto err_out; 
    }

    *ppos += len;
    ret = len;

    return ret;

err_out:
    return ret;
}

/*----------------------------------------------模块加载/卸载入口------------------------------------------------------*/
static struct platform_driver dts_gpio_driver = 
{
    .probe = gpio_probe,
    .remove = gpio_remove,
    .driver = {
        .name = GPIO_DEV_NAME,
        .owner = THIS_MODULE,
        .of_match_table = gpio_of_match, 
    },
};

static int __init gpio_init(void)
{
    int ret;
    /* 动态分配设备号 */
    ret = alloc_chrdev_region(&g_gpio_devno, 0, GPIO_DEV_CNT, GPIO_DEV_NAME);
    if (ret < 0) 
    {
        pr_err("alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }
    
    /* 注册平台驱动 */
    ret = platform_driver_register(&dts_gpio_driver);
    if (ret < 0) 
    {
        pr_err("platform_driver_register failed: %d\n", ret);
        unregister_chrdev_region(g_gpio_devno, GPIO_DEV_CNT); /* 失败要回滚 */
    }
    return ret;
}

static void __exit gpio_exit(void)
{
    /* 先注销驱动，再释放设备号 */
    platform_driver_unregister(&dts_gpio_driver);
    unregister_chrdev_region(g_gpio_devno, GPIO_DEV_CNT);
    pr_info("dts_gpio module removed\n");
}

module_init(gpio_init);      
module_exit(gpio_exit);      

MODULE_LICENSE("GPL");