#include "linux/stddef.h"
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/input/mt.h>
#include <linux/random.h>
#include <linux/backlight.h>

#if 1
#define MY_DEBUG(fmt,arg...)  printk("gp7101_bl:%s %d "fmt"",__FUNCTION__,__LINE__,##arg);
#else
#define MY_DEBUG(fmt,arg...)
#endif

/* I2C 背光控制器寄存器定义 */
#define BACKLIGHT_REG_CTRL_8    0x03  
#define BACKLIGHT_REG_CTRL_16 0x02

#define BACKLIGHT_NAME      "gp7101-backlight"

/* 背光控制器设备数据结构 */
struct gp7101_backlight_data {
    /* 指向一个i2c_client结构体的指针*/
    struct i2c_client *client;

};

/* 设置背光亮度 */
static int gp7101_backlight_set(struct backlight_device *bl)
{
    struct gp7101_backlight_data *data = bl_get_data(bl);  // 获取背光数据结构指针
    struct i2c_client *client = data->client;  // 获取I2C设备指针
    u8 addr[1] = {BACKLIGHT_REG_CTRL_8};  // 定义I2C地址数组
    u8 buf[1] = {bl->props.brightness};  // 定义数据缓冲区，用于存储背光亮度值

    MY_DEBUG("pwm:%d", bl->props.brightness);  // 输出背光亮度值

    // 将背光亮度值写入设备
    i2c_write(client, addr, sizeof(addr), buf, sizeof(buf));

    return 0;  // 返回成功
}

/* 背光设备操作函数 */
static struct backlight_ops gp7101_backlight_ops = {
    .update_status = gp7101_backlight_set,
};

// gp7101_bl_probe - 探测函数，当I2C总线上的设备与驱动匹配时会被调用
// 初始化驱动
static int gp7101_bl_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    struct backlight_device *bl; // backlight_device结构用于表示背光设备
    struct gp7101_backlight_data *data; // 自定义的背光数据结构
    struct backlight_properties props; // 背光设备的属性
    struct device_node *np = client->dev.of_node; // 设备树中的节点

    MY_DEBUG("locat"); // 打印调试信息

    // 为背光数据结构动态分配内存
    data = devm_kzalloc(&client->dev, sizeof(struct gp7101_backlight_data), GFP_KERNEL);
    if (data == NULL){
        dev_err(&client->dev, "Alloc GFP_KERNEL memory failed."); // 内存分配失败，打印错误信息
        return -ENOMEM; // 返回内存分配错误码
    } 

    // 初始化背光属性结构
    memset(&props, 0, sizeof(props));
    props.type = BACKLIGHT_RAW; // 设置背光类型为原始类型
    props.max_brightness = 255; // 设置最大亮度为255

    // 从设备树中读取最大亮度级别
    of_property_read_u32(np, "max-brightness-levels", &props.max_brightness);

    // 从设备树中读取默认亮度级别
    of_property_read_u32(np, "default-brightness-level", &props.brightness);

    // 确保亮度值在有效范围内
    if(props.max_brightness>255 || props.max_brightness<0){
        props.max_brightness = 255;
    }
    if(props.brightness>props.max_brightness || props.brightness<0){
        props.brightness = props.max_brightness;
    }

    // 注册背光设备
    bl = devm_backlight_device_register(&client->dev, "backlight", &client->dev, data, &gp7101_backlight_ops,&props);
    if (IS_ERR(bl)) {
        dev_err(&client->dev, "failed to register backlight device\n"); // 注册失败，打印错误信息
        return PTR_ERR(bl); // 返回错误码
    }
    data->client = client; // 保存i2c_client指针
    i2c_set_clientdata(client, data); // 设置i2c_client的客户端数据

    MY_DEBUG("max_brightness:%d brightness:%d",props.max_brightness, props.brightness); // 打印最大亮度和当前亮度
    backlight_update_status(bl); // 更新背光设备的状态

    return 0; // 返回成功
}

/* IIC  write函数 */
s32 i2c_write(struct i2c_client *client, u8 *addr, u8 addr_len, u8 *buf, s32 len)
{
    struct i2c_msg msg; // 定义i2c消息结构，用于传输数据
    s32 ret = -1; // 初始化返回值为-1，表示失败
    u8 *temp_buf; // 定义临时缓冲区指针

    msg.flags = !I2C_M_RD; // 标志位，表示写操作
    msg.addr = client->addr; // 设备地址
    msg.len = len + addr_len; // 写入数据的总长度（地址长度+数据长度）

    // 分配临时缓冲区
    temp_buf = kzalloc(msg.len, GFP_KERNEL);
    if (!temp_buf) {
        goto error; // 如果分配失败，跳转到错误处理
    }

    // 装填地址到临时缓冲区
    memcpy(temp_buf, addr, addr_len);
    // 装填数据到临时缓冲区（紧随地址之后）
    memcpy(temp_buf + addr_len, buf, len);
    msg.buf = temp_buf; // 设置消息的缓冲区为临时缓冲区

    // 发送消息并写入数据
    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret == 1) {
        kfree(temp_buf); // 释放临时缓冲区
        return 0; // 如果消息成功传输，返回0表示成功
    }

error:
    // 如果写入失败，打印错误信息
    if (addr_len == 2) {
        MY_DEBUG("I2C Write: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    } else {
        MY_DEBUG("I2C Write: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    if (temp_buf) {
        kfree(temp_buf); // 释放临时缓冲区
    }
    return -1; // 返回-1表示失败
}

s32 i2c_read(struct i2c_client *client,u8 *addr,u8 addr_len, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = addr_len;
    msgs[0].buf   = &addr[0];
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len;
    msgs[1].buf   = &buf[0];

    ret = i2c_transfer(client->adapter, msgs, 2);
    if(ret == 2)return 0;

    if(addr_len == 2){
        MY_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        MY_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    
    return -1;
}

/*----------------------------------------------------------------------------------------- */
static int gp7101_bl_remove(struct i2c_client *client)
{
    MY_DEBUG("locat");
    return 0;
}

static const struct of_device_id gp7101_bl_of_match[] = {
    { .compatible = BACKLIGHT_NAME, },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gp7101_bl_of_match);

static struct i2c_driver gp7101_bl_driver = {
    .probe      = gp7101_bl_probe,
    .remove     = gp7101_bl_remove,
    .driver = {
        .name     = BACKLIGHT_NAME,
     .of_match_table = of_match_ptr(gp7101_bl_of_match),
    },
};

static int __init my_init(void)
{
    MY_DEBUG("locat");
    return i2c_add_driver(&gp7101_bl_driver);
}

static void __exit my_exit(void)
{
    MY_DEBUG("locat");
    i2c_del_driver(&gp7101_bl_driver);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("My touch driver");
MODULE_AUTHOR("rubo");
