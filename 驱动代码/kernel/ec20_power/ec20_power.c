#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <asm/uaccess.h>
#include <asm/io.h>
/***************************************************************
文件名   	: ec20_power.c
作者      	: 千古长夜
版本      	: V1.0
描述      	: EC20 4g模块电源控制。
***************************************************************/
#define EC20POWER_CNT      		1           	/* 设备号个数 	*/
#define EC20POWER_NAME        	"ec20_power" 	/* 名字 		*/
#define EC20POWEROFF              	0           /* 关灯 			*/
#define EC20POWERON               	1           /* 开灯 			*/

/* ec20_power设备结构体 */
struct ec20_power_dev{
    dev_t devid;          		/* 设备号     	*/
    struct cdev cdev;    		/* cdev     	*/
    struct class *class;  	    /* 类      		*/
    struct device *device;  	/* 设备    		*/
    int major;              	/* 主设备号   	*/
    int minor;              	/* 次设备号   	*/
    struct device_node  *nd;    /* 设备节点 	*/
    int pwd_gpio;           	/* ec20power所使用的GPIO编号   */
};

struct ec20_power_dev ec20_power; /* ec20power设备 	*/

/*
 * @description  	: 打开设备
 * @param – inode	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 *                    一般在open的时候将private_data指向设备结构体。
 * @return       	: 0 成功;其他 失败
 */
static int ec20power_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &ec20_power; /* 设置私有数据 */
    return 0;
}

/*
 * @description   : 从设备读取数据 
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - buf  	: 返回给用户空间的数据缓冲区
 * @param - cnt  	: 要读取的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return        	: 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t ec20power_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

/*
 * @description 	: 向设备写数据 
 * @param - filp 	: 设备文件，表示打开的文件描述符
 * @param - buf  	: 要写给设备写入的数据
 * @param - cnt  	: 要写入的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return        	: 写入的字节数，如果为负值，表示写入失败
 */
static ssize_t ec20power_write(struct file *filp, const char __user *buf, 
		                         size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ec20powerstat;
    struct ec20_power_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf, buf, cnt); 
    if(retvalue < 0) {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    ec20powerstat = databuf[0];       /* 获取状态值 */

    if(ec20powerstat == EC20POWERON) {  
        gpio_set_value(dev->pwd_gpio, 1);   /* 打开EC20POWER */
    } else if(ec20powerstat == EC20POWEROFF) {
        gpio_set_value(dev->pwd_gpio, 0);   /* 关闭EC20POWER */
    }
    return 0;
}

/*
 * @description   : 关闭/释放设备
 * @param - filp 	: 要关闭的设备文件(文件描述符)
 * @return        	: 0 成功;其他 失败
 */
static int ec20power_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* 设备操作函数 */
static struct file_operations ec20_power_fops = {
    .owner = THIS_MODULE,
    .open = ec20power_open,
    .read = ec20power_read,
    .write = ec20power_write,
    .release =  ec20power_release,
};

/*
 * @description 	: 驱动出口函数
 * @param       	: 无
 * @return      	: 无
 */
static int __init ec20power_init(void)
{
    int ret = 0;
    const char *str;

    /* 设置所使用的GPIO */
    /* 1、获取设备节点：ec20_power */
    ec20_power.nd = of_find_node_by_path("/ec20_power");
    if(ec20_power.nd == NULL) {
        printk("ec20_power node not find!\r\n");
        return -EINVAL;
    }

    /* 2.读取status属性 */
    ret = of_property_read_string(ec20_power.nd, "status", &str);
    if(ret < 0) 
        return -EINVAL;

    if (strcmp(str, "okay"))
        return -EINVAL;
    
    /* 3、获取compatible属性值并进行匹配 */
    ret = of_property_read_string(ec20_power.nd, "compatible", &str);
    if(ret < 0) {
        printk("ec20_power: Failed to get compatible property\n");
        return -EINVAL;
    }

    if (strcmp(str, "ec20_power")) {
        printk("ec20_power: Compatible match failed\n");
        return -EINVAL;
    }

    /* 4、 获取设备树中的gpio属性，得到所使用的编号 */
    ec20_power.pwd_gpio = of_get_named_gpio(ec20_power.nd, "ec20power-gpio", 0);
    if(ec20_power.pwd_gpio < 0) {
        printk("can't get ec20power-gpio");
        return -EINVAL;
    }
    printk("ec20power-gpio num = %d\r\n", ec20_power.pwd_gpio);

    /* 5.向gpio子系统申请使用GPIO */
    ret = gpio_request(ec20_power.pwd_gpio, "EC20POWER-GPIO");
    if (ret) {
        printk(KERN_ERR "ec20_power: Failed to request ec20power-gpio\n");
        return ret;
    }

    /* 6、设置GPIO为输出，并且输出高电平，默认开启EC20 */
    ret = gpio_direction_output(ec20_power.pwd_gpio, 1);
    if(ret < 0) {
        printk("can't set gpio!\r\n");
    }

    /* 注册字符设备驱动 */
    /* 1、创建设备号 */
    if (ec20_power.major) {        /*  定义了设备号 */
        ec20_power.devid = MKDEV(ec20_power.major, 0);
        ret = register_chrdev_region(ec20_power.devid, EC20POWER_CNT, EC20POWER_NAME);
        if(ret < 0) {
            pr_err("cannot register %s char driver [ret=%d]\n", EC20POWER_NAME, EC20POWER_CNT);
            goto free_gpio;
        }
    } else {                        /* 没有定义设备号 */
        ret = alloc_chrdev_region(&ec20_power.devid, 0, EC20POWER_CNT, EC20POWER_NAME);    /* 申请设备号 */
        if(ret < 0) {
            pr_err("%s Couldn't alloc_chrdev_region, ret=%d\r\n", EC20POWER_NAME, ret);
            goto free_gpio;
        }
        ec20_power.major = MAJOR(ec20_power.devid); /* 获取分配号的主设备号 */
        ec20_power.minor = MINOR(ec20_power.devid); /* 获取分配号的次设备号 */
    }
    printk("ec20_power major=%d,minor=%d\r\n",ec20_power.major, ec20_power.minor);   
    
    /* 2、初始化cdev */
    ec20_power.cdev.owner = THIS_MODULE;
    cdev_init(&ec20_power.cdev, &ec20_power_fops);
    
    /* 3、添加一个cdev */
    cdev_add(&ec20_power.cdev, ec20_power.devid, EC20POWER_CNT);
    if(ret < 0)
        goto del_unregister;
        
    /* 4、创建类 */
    ec20_power.class = class_create(THIS_MODULE, EC20POWER_NAME);
    if (IS_ERR(ec20_power.class)) {
        goto del_cdev;
    }

    /* 5、创建设备 */
    ec20_power.device = device_create(ec20_power.class, NULL, ec20_power.devid, NULL, EC20POWER_NAME);
    if (IS_ERR(ec20_power.device)) {
        goto destroy_class;
    }
    return 0;
    
destroy_class:
    class_destroy(ec20_power.class);
del_cdev:
    cdev_del(&ec20_power.cdev);
del_unregister:
    unregister_chrdev_region(ec20_power.devid, EC20POWER_CNT);
free_gpio:
    gpio_free(ec20_power.pwd_gpio);
    return -EIO;
}

/*
 * @description 	: 驱动出口函数
 * @param       	: 无
 * @return      	: 无
 */
static void __exit ec20power_exit(void)
{
    /* 注销字符设备驱动 */
    cdev_del(&ec20_power.cdev);/*  删除cdev */
    unregister_chrdev_region(ec20_power.devid, EC20POWER_CNT);
    device_destroy(ec20_power.class, ec20_power.devid);/* 注销设备 */
    class_destroy(ec20_power.class);/* 注销类 		*/
    gpio_free(ec20_power.pwd_gpio); /* 释放GPIO 	*/
}

module_init(ec20power_init);
module_exit(ec20power_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("YOUMENG");
MODULE_INFO(intree, "Y");

