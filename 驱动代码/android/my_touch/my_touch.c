#include "linux/stddef.h"
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
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

#define MY_SWAP(x, y)                 do{\
                                         typeof(x) z = x;\
                                         x = y;\
                                         y = z;\
                                       }while (0)

#if 1
#define MY_DEBUG(fmt,arg...)  printk("MY_TOUCH:%s %d "fmt"",__FUNCTION__,__LINE__,##arg);
#else
#define MY_DEBUG(fmt,arg...)
#endif

// 定义一个表示触摸设备的结构体
struct my_touch_dev {
    struct i2c_client *client; // 指向与触摸设备通信的 I2C 客户端结构体的指针
    struct input_dev *input_dev; // 指向与输入设备关联的 input_dev 结构体的指针，用于处理输入事件
    int rst_pin; // 触摸设备的复位引脚编号
    int irq_pin; // 触摸设备的中断引脚编号
    u32 abs_x_max; // 触摸设备在 X 轴上的最大绝对值
    u32 abs_y_max; // 触摸设备在 Y 轴上的最大绝对值
    int irq; // 触摸设备的中断号
};

s32 my_touch_i2c_read(struct i2c_client *client,u8 *addr,u8 addr_len, u8 *buf, s32 len)
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

s32 my_touch_i2c_write(struct i2c_client *client, u8 *addr, u8 addr_len, u8 *buf,s32 len)
{
    struct i2c_msg msg;
    s32 ret = -1;
    u8 *temp_buf;

    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len+addr_len;

    temp_buf= kzalloc(msg.len, GFP_KERNEL);
    if (!temp_buf){
        goto error;
    }
    
    // 装填地址
    memcpy(temp_buf, addr, addr_len);
    // 装填数据
    memcpy(temp_buf + addr_len, buf, len);
    msg.buf = temp_buf;

    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret == 1) {
        kfree(temp_buf);
        return 0;
    }

error:
    if(addr_len == 2){
        MY_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(addr[0] << 8)) | addr[1]), len, ret);
    }else {
        MY_DEBUG("I2C Read: 0x%02X, %d bytes failed, errcode: %d! Process reset.", addr[0], len, ret);
    }
    if (temp_buf)
        kfree(temp_buf);
    return -1;
}

s32 gt9271_read_version(struct i2c_client *client)
{
    s32 ret = -1;
    u8 addr[1] = {0xA1};
    u8 buf[3] = {0};


    ret = my_touch_i2c_read(client, addr,sizeof(addr), buf, sizeof(buf));
    if (ret < 0){
        MY_DEBUG("GTP read version failed");
        return ret;
    }

    if (buf[5] == 0x00){
        MY_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    else{
        MY_DEBUG("IC Version: %0x %0x_%02x", buf[0], buf[1], buf[2]);
    }
    return ret;
}


/* 探针函数 */
static int my_touch_ts_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    int ret; // 定义一个返回值变量
    struct my_touch_dev *ts; // 定义一个结构体指针，用来指向my_touch_dev结构体
    struct device_node *np = client->dev.of_node; // 获取设备节点
    // 打印调试信息
    MY_DEBUG("locat"); // 调用MY_DEBUG函数打印调试信息，此处打印"locat"

    ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL); // 使用devm_kzalloc分配内存，减少内存申请操作
    if (ts == NULL){ // 检查内存分配是否成功
        dev_err(&client->dev, "Alloc GFP_KERNEL memory failed."); // 内存分配失败，打印错误信息
        return -ENOMEM; // 返回内存申请错误的码
    }
    ts->client = client; // 触摸屏设备的客户端指针指向i2c_client结构体
    i2c_set_clientdata(client, ts); // 将my_touch_dev结构体的指针设置为i2c客户端的数据

    // 从设备树中读取触摸屏的最大X和Y值
    if (of_property_read_u32(np, "max-x", &ts->abs_x_max)) {
        dev_err(&client->dev, "no max-x defined\n"); // 如果读取最大X值失败，打印错误信息
        return -EINVAL; // 返回参数无效的错误码
    }
    MY_DEBUG("abs_x_max:%d",ts->abs_x_max); // 打印X值

    if (of_property_read_u32(np, "max-y", &ts->abs_y_max)) {
        dev_err(&client->dev, "no max-y defined\n"); // 如果读取最大Y值失败，打印错误信息
        return -EINVAL; // 返回参数无效的错误码
    }
    MY_DEBUG("abs_x_max:%d",ts->abs_y_max); // 打印Y值

    // 获取并请求复位GPIO管脚
    ts->rst_pin = of_get_named_gpio(np, "reset-gpio", 0); // 从设备树中获取复位管脚
    ret = devm_gpio_request(&client->dev,ts->rst_pin,"my touch touch gpio"); // 请求使用复位管脚
    if (ret < 0){ // 如果请求失败
        dev_err(&client->dev, "gpio request failed."); // 打印错误信息
        return -ENOMEM;                                 // 返回内存申请错误的码
    }
    
    ts->irq_pin = of_get_named_gpio(np, "touch-gpio", 0); // 从设备树中获取中断管脚
    ret = devm_gpio_request_one(&client->dev, ts->irq_pin, // 请求使用中断管脚
                GPIOF_IN, "my touch touch gpio");
    if (ret < 0)
        return ret; // 如果请求失败，直接返回错误码

    // 复位触摸屏设备
    gpio_direction_output(ts->rst_pin,0); // 设置复位管脚输出低电平
    msleep(20); // 等待20毫秒
    gpio_direction_output(ts->irq_pin,0); // 设置中断管脚输出低电平
    msleep(2); // 等待2毫秒
    gpio_direction_output(ts->rst_pin,1); // 设置复位管脚输出高电平
    msleep(6); // 等待6毫秒
    gpio_direction_output(ts->irq_pin, 0); // 设置中断管脚输出低电平
    msleep(50); // 等待50毫秒

    // 申请中断服务
    ts->irq = gpio_to_irq(ts->irq_pin); // 将GPIO管脚转换为中断号
    if(ts->irq){ // 检查中断号是否有效
        ret = devm_request_threaded_irq(&(client->dev), ts->irq, // 请求线程化中断
                NULL, my_touch_irq_handler,                      // 中断服务函数
                IRQF_TRIGGER_FALLING | IRQF_ONESHOT,             // 中断触发方式为下降沿触发，且只触发一次
                client->name, ts);
        if (ret != 0) {
            MY_DEBUG("Cannot allocate ts INT!ERRNO:%d\n", ret); // 如果中断请求失败，打印错误信息
            return ret; // 返回错误码
        }
    }

    // 使用devm_input_allocate_device分配输入设备对象
    ts->input_dev = devm_input_allocate_device(&client->dev); 
    if (!ts->input_dev) { // 检查输入设备对象是否分配成功
        dev_err(&client->dev, "Failed to allocate input device.\n"); // 打印错误信息
        return -ENOMEM; // 返回内存申请错误的码
    }

    // 设置输入设备的名称
    ts->input_dev->name = "my touch screen"; 
    // 设置输入设备的总线类型为I2C
    ts->input_dev->id.bustype = BUS_I2C; 
    
    // 设置X轴的最大值为480
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, 480, 0, 0); 
    // 设置Y轴的最大值为800
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, 800, 0, 0); 

    // 初始化5个多点触摸槽位，直接模式
    ret = input_mt_init_slots(ts->input_dev, 5, INPUT_MT_DIRECT); 
    if (ret) {
        dev_err(&client->dev, "Input mt init error\n"); // 打印错误信息
        return ret; // 返回错误码
    }

    // 注册输入设备
    ret = input_register_device(ts->input_dev); // 注册输入设备
    if (ret)
        return ret; // 返回错误码

    // 读取版本号
    gt9271_read_version(client); 

    return 0; // 如果一切顺利，返回0
}

/* 中断处理函数 */
// 通过 IIC 去读取屏幕相关参数
static irqreturn_t my_touch_irq_handler(int irq, void *dev_id)
{
    s32 ret = -1;                        // 定义一个返回值，初始化为-1
    struct my_touch_dev *ts = dev_id;    // 获取指向设备数据的指针
    u8 addr[1] = {0x02};                 // 定义一个寄存器地址数组对应数据手册TD_STATUS 
    u8 point_data[1+6*5]={0};            // 定义一个点数据数组，预留足够空间 for 5 touch points
    u8 touch_num = 0;                    // 定义一个变量来存储触摸点的数量
    u8 *touch_data;                       // 定义一个指针用于指向点数据
    int i = 0;                           // 定义一个循环变量
    int event_flag, touch_id, input_x, input_y; // 定义一些用于存储事件信息的变量

    MY_DEBUG("irq");                    // 打印中断信息

    ret = my_touch_i2c_read(ts->client, addr, sizeof(addr), point_data, sizeof(point_data)); // 尝试读取触摸屏设备的数据
    if (ret < 0){                        // 如果读取失败
        MY_DEBUG("I2C write end_cmd error!"); // 打印错误信息
    }
    touch_num = point_data[0]&0x0f;     // 获取触摸点的数量
    MY_DEBUG("touch_num:%d",touch_num); // 打印触摸点数量

    // 遍历触摸点数据
    for(i=0; i<5; i++){
        // 获取触摸点数据
        touch_data = &point_data[1+6*i];
        /*
        解析触摸点的事件标志位
        00b: 按下
        01b: 抬起
        10b: 接触
        11b: 保留
        */
        event_flag = touch_data[0] >> 6;
        if(event_flag == 0x03)continue; // 如果事件标志位不是按下或抬起，则跳过此循环
        touch_id = touch_data[2] >> 4;    // 获取触摸点ID

        MY_DEBUG("i:%d touch_id:%d event_flag:%d",i,touch_id,event_flag); // 打印调试信息
        input_x  = ((touch_data[0]&0x0f)<<8) | touch_data[1]; // 计算X坐标
        input_y  = ((touch_data[2]&0x0f)<<8) | touch_data[3]; // 计算Y坐标

        // MY_SWAP(input_x,input_y); // 如果需要交换X和Y坐标，可以取消注释此行
        MY_DEBUG("i:%d,x:%d,y:%d",i,input_x,input_y); // 打印调试信息
        // 设置输入设备的触摸槽位
        input_mt_slot(ts->input_dev, touch_id);

        if(event_flag == 0){ // 如果是按下
            // 上报按下事件和坐标
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true); // 设置为按下状态
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 480-input_x); // 报告X坐标
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y); // 报告Y坐标
        }else if (event_flag == 2){ // 如果是长按
            // 直接上报数据
            input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 480-input_x); // 报告X坐标
            input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y); // 报告Y坐标
        }
        else if(event_flag == 1){ // 如果是触摸抬起
            // 上报事件
            input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false); // 设置为抬起状态
        }
    }
    // 报告输入设备的指针仿真信息
    input_mt_report_pointer_emulation(ts->input_dev, true);
    // 同步输入事件
    input_sync(ts->input_dev);
    // 返回IRQ_HANDLED，表示中断已经被处理
    return IRQ_HANDLED;
}



static int my_touch_ts_remove(struct i2c_client *client)
{
    struct my_touch_dev *ts = i2c_get_clientdata(client);
    MY_DEBUG("locat");
    input_unregister_device(ts->input_dev);
    return 0;
}

static const struct of_device_id my_touch_of_match[] = {
    { .compatible = "my,touch", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_touch_of_match);

static struct i2c_driver my_touch_ts_driver = {
    .probe      = my_touch_ts_probe,
    .remove     = my_touch_ts_remove,
    .driver = {
        .name     = "my-touch",
     .of_match_table = of_match_ptr(my_touch_of_match),
    },
};

static int __init my_ts_init(void)
{
    MY_DEBUG("locat");
    return i2c_add_driver(&my_touch_ts_driver);
}

static void __exit my_ts_exit(void)
{
    MY_DEBUG("locat");
    i2c_del_driver(&my_touch_ts_driver);
}

module_init(my_ts_init);
module_exit(my_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("My touch driver");
MODULE_AUTHOR("rubo");