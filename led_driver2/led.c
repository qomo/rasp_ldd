#include <linux/init.h>
#include <linux/timer.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

// include RPi harware specific constants
// GPIO_BASE SZ_4K
#include <mach/hardware.h>

#define LED_DRIVER_NAME   "led"
#define DEV_COUNT   1

/* GPIO macros */
#define GPIO_SET_OUT(g)     *(gpio+((g)/10)) |= (1<<(((g)%10)*3))
#define GPIO_SET_PIN(g)     *(gpio+7) = 1<<g;
#define GPIO_CLR_PIN(g)     *(gpio+10) = 1<<g;

struct timer_list led_timer;
char kbledstatus = 0;
int status = 0;

static int gpio_pin = 17;   //default GPIO pin
// Possible valid GPIO pins
int valid_gpio_pins[] = {0, 1, 4, 8, 7, 9, 10, 11, 14, 15, 17, 18, 21, 22, 23, 24, 25};
volatile unsigned *gpio;

// 定义设备模型
struct led_dev {
    struct cdev cdev;
    struct mutex mutex;
    int blink_freq;
};
struct led_dev *led_devices;    // allocated in led_init
//声明设备号
static dev_t dev_number;
static int led_major;


// Initialise GPIO memory
static int init_port(void)
{
    // reserve GPIO memory region.
    if (request_mem_region(GPIO_BASE, SZ_4K, LED_DRIVER_NAME)){
        printk(KERN_ERR LED_DRIVER_NAME ": unable to obtain GPIO I/O memory address\n");
        return -EBUSY;
    }

    // remap the GPIO memory 
    if ((gpio = ioremap_nocache(GPIO_BASE, SZ_4K)) == NULL) {
        printk(KERN_ERR LED_DRIVER_NAME ": failed to map GPIO I/O memory\n");
        return -EBUSY;
    }

    return 0;
}


/* timer blink function */

void led_timer_fun(unsigned long ptr)
{
	printk(KERN_ALERT "jiffies + HZ = %ld + %d\n", jiffies, HZ);
    if (status == 0)
    {
        GPIO_SET_PIN(gpio_pin);
        status = 1;
    } else {
        GPIO_CLR_PIN(gpio_pin);
        status = 0;
    }

    led_timer.expires = jiffies + HZ;
    add_timer(&led_timer);
}


// 在同步状态下读取寄存器的值
static ssize_t led_get_val(struct led_dev* dev, char* buf)
{
    int val = 0;

    if(mutex_lock_interruptible(&(dev->mutex)))
        return -ERESTARTSYS;

    val = dev->blink_freq;
    mutex_unlock(&(dev->mutex));

    return snprintf(buf, 30, "%d\n", val);
}

// 在同步状态下设置寄存器的值
static ssize_t led_set_val(struct led_dev* dev, const char* buf, size_t count)
{
    int val = 0;

    val = (int)simple_strtol(buf, NULL, 10);

    if(mutex_lock_interruptible(&(dev->mutex)))
        return -ERESTARTSYS;

    dev->blink_freq = val;
    mutex_unlock(&(dev->mutex));

    return count;
}

// /proc 节点的写操作
ssize_t led_proc_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_ops)
{
    int err = 0;
    char* page = NULL;

    if(count > PAGE_SIZE)
    {
        printk(KERN_ALERT "The buff is too large: %lu.\n", (unsigned long)count);
        return -EFAULT;
    }

    page = (char*)__get_free_page(GFP_KERNEL);
    if(!page)
    {
        printk(KERN_ALERT "Failed to alloc page.\n");
        return -ENOMEM;
    }

    if(copy_from_user(page, buf, count))
    {
        printk(KERN_ALERT "Failed to copy buff from user.\n");
        err = -EFAULT;
        goto out;
    }

    err = led_set_val(led_devices, page, count);

out:
    free_page((unsigned long)page);
    return err;
}

// /proc 节点读操作
// ssize_t led_proc_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
// {
//     if(off > 0)
//     {
//         // *eof = 1;
//         return 0;
//     }

//     return led_get_val(led_devices, buf);
// }

// 创建／proc节点
static void led_create_proc(void)
{
    struct proc_dir_entry* entry;

    // entry = proc_create(LED_DRIVER_NAME, 0, NULL);
    // if(entry)
    // {
    //     // entry->owner = THIS_MODULE;
    //     entry->read_proc = led_proc_read;
    //     entry->write_proc = led_proc_write;
    // }
}

// 删除／proc节点
static void led_remove_proc(void)
{
    remove_proc_entry(LED_DRIVER_NAME, NULL);
}


int led_open(struct inode *inode, struct file *filp)
{
    struct led_dev *dev;    //device information

    dev = container_of(inode->i_cdev, struct led_dev, cdev);
    filp->private_data = dev;   // for other methods

    return 0;
}

int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *f_ops)
{
    ssize_t ret = 0;
    struct led_dev *dev = filp->private_data;
    unsigned char buf_tmp[30];

    if(mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    snprintf(buf_tmp, 30, "%d\n", dev->blink_freq);

    if(count < sizeof(buf_tmp))
        goto out;

    if(copy_to_user(buf, &buf_tmp, sizeof(dev->blink_freq)))
    {
        ret = -EFAULT;
        goto out;
    }

    ret = sizeof(buf_tmp);

out:
    mutex_unlock(&(dev->mutex));
    return ret;
}

ssize_t led_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_ops)
{
    ssize_t ret = 0;
    struct led_dev *dev = filp->private_data;

    if(mutex_lock_interruptible(&(dev->mutex)))
        return -ERESTARTSYS;

    if(count != sizeof(dev->blink_freq))
        goto out;

    if(copy_from_user(&(dev->blink_freq), buf, count))
    {
        ret = -EFAULT;
        goto out;
    }

    ret = sizeof(dev->blink_freq);

out:
    mutex_unlock(&(dev->mutex));
    return ret;
}

struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .read = led_read,
    .write = led_proc_write,
    .open = led_open,
    .release = led_release,
};

static void led_dev_setup_cdev(struct led_dev *dev, int index)
{
    int ret;
    dev_t devno = MKDEV(led_major, index);

    cdev_init(&dev->cdev, &led_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &led_fops;
    ret = cdev_add(&dev->cdev, devno, 1);
    if(ret){
        printk("adding led cdev error");
    }
    printk("Setup led cdev success!");
}

void led_exit(void)
{
    // release mapped memory allocated region.
    if(gpio != NULL){
        iounmap(gpio);
        release_mem_region(GPIO_BASE, SZ_4K);
        printk(LED_DRIVER_NAME ": cleaned up resources\n");
    }
    del_timer(&led_timer);

    led_remove_proc();

    if(led_devices){
        kfree(led_devices);
    }
    unregister_chrdev_region(dev_number, DEV_COUNT);
    printk(KERN_ALERT "goodbye world!\n");
}

static int led_init(void)
{
    int result, i;
    printk(KERN_ALERT "Hello, LED world!\n");

    // check for valid gpio pin number
    result = 0;
    for(i=0; (i < ARRAY_SIZE(valid_gpio_pins)) && (result != 1); i++){
        if(gpio_pin == valid_gpio_pins[i])
            result++;
    }
    if(result != 1){
        result = -EINVAL;
        printk(KERN_ERR LED_DRIVER_NAME ": invalid GPIO pin specified!\n");
        goto exit_rpi;
    }

    result = alloc_chrdev_region(&dev_number, 0, DEV_COUNT, LED_DRIVER_NAME);
    if(result)
    {
        printk("alloc device number fail\n");
        return result;
    }
    led_major = MAJOR(dev_number);
    // 如果申请成功，打印主设备号
    printk("major number: %d\n", led_major);

    led_devices = kmalloc(DEV_COUNT*sizeof(struct led_dev), GFP_KERNEL);
    if(!led_devices){
        result = -ENOMEM;
        goto fail;
    }
    memset(led_devices, 0, sizeof(struct led_dev));

    mutex_init(&(led_devices->mutex));
    led_dev_setup_cdev(led_devices, 0);

    // 将led_dev保存在设备私有数据区
    // dev_set_drvdata(led_devices, led_dev);

    // 创建proc节点
    led_create_proc();


    init_port();
    GPIO_SET_OUT(gpio_pin);

    // timer blink 
    init_timer(&led_timer);
    led_timer.function = led_timer_fun;
    led_timer.data = (unsigned long) &kbledstatus;
    led_timer.expires = jiffies + HZ;
    add_timer(&led_timer);

    return 0;

fail:
    led_exit();
    return result;

exit_rpi:
    return result;
}

MODULE_LICENSE("GPL");
module_init(led_init);
module_exit(led_exit);
