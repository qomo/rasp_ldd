#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/time.h>
#include <linux/slab.h>

/* User-defined macros */
#define NUM_GPIO_PINS       21
#define MAX_GPIO_NUMBER     32
#define DEVICE_NAME         "raspi-gpio"
#define BUF_SIZE            512
#define INTERRUPT_DEVICE_NAME   "gpio interrupt"

/* User-difined data types */
enum state  {low, high};
enum direction {in, out};

/* 
 * struct raspi_gpio_dev - Per gpio pin data structure
 * @cdev:       instance of struct cdev
 * @pin:        instance of struct gpio
 * @state:      logic state (low, high) of a GPIO pin 
 * @dir:        direction of a GPIO pin 
 * @irq_perm:   used to enable/disable interrupt on GPIO pin 
 * @irq_flag:   used to indicate rising/falling edge trigger
 * @lock:       used to protect atomic code section
 */
struct raspi_gpio_dev {
    struct cdev cdev;
    struct gpio pin;
    enum state state;
    enum direction dir;
    bool irq_perm;
    unsigned long irq_flag;
    unsigned int irq_counter;
    spinlock_t lock;
};

/* Declaration of entry points */
// static int raspi_gpio_open(struct inode *inode, struct file *filp);
// static ssize_t raspi_gpio_read(struct file *filp,
//                               char *buf,
//                               size_t count,
//                               loff_t *f_pos);
// static ssize_t raspi_gpio_write(struct file *filp,
//                                const char *buf,
//                                size_t count,
//                                loff_t *f_pos);
// static int raspi_gpio_release(struct inode, struct file *filp);

/* File operation structure */
static struct file_operations raspi_gpio_fops = {
    // .owner  = THIS_MODULE,
    // .open   = raspi_gpio_open,
    // .release= raspi_gpio_release,
    // .read   = raspi_gpio_read,
    // .write  = raspi_gpio_write,
};

// /* Forward declaration of functions */
// static int raspi_gpio_init(void);
// static void raspi_gpio_exit(void);
// unsigned int millis(void);
// static irqreturn_t irq_handler(int irq, void *arg);

/* Global varibles for GPIO driver */
struct raspi_gpio_dev *raspi_gpio_devp[NUM_GPIO_PINS];
static dev_t first;
static struct class *raspi_gpio_class;
static unsigned int last_interrupt_time = 0;
static uint64_t epochMilli;


bool is_valid_pin(int pin)
{
  return (pin != 0 && pin != 1 && pin != 5 && pin != 6 && pin != 12 && pin != 13 && pin != 16 && pin != 19 && pin != 20 && pin != 21 && pin != 22);
}

/*
 * raspi_gpio_init - Initialize GPIO device driver
 * 
 * This function performs the following tasks:
 * Dynamically register a character device major
 * Create "raspi-gpio" class
 * Claim GPIO resource
 * Initialize the per-device data structure raspi_gpio_dev
 * Initialize spin lock used for synchronization
 * Register character device to the kernel
 * Create device nodes to expose GPIO resource
 */
static int __init raspi_gpio_init(void)
{
  int i, ret, index = 0;
  struct timeval tv;

  if (alloc_chrdev_region(&first,
                          0,
                          NUM_GPIO_PINS,
                          DEVICE_NAME) < 0) {
    printk(KERN_DEBUG "Cannot register device\n");
    return -1;
  }

  if ((raspi_gpio_class = class_create( THIS_MODULE,
                                        DEVICE_NAME)) == NULL){
    printk(KERN_DEBUG "Cannot create class %s\n", DEVICE_NAME);
    unregister_chrdev_region(first, NUM_GPIO_PINS);
    return -EINVAL;
  }

  for (i=0; i<MAX_GPIO_NUMBER; i++) {
    if (is_valid_pin(i)) {
      raspi_gpio_devp[index] = kmalloc(sizeof(struct raspi_gpio_dev), GFP_KERNEL);

      if (!raspi_gpio_devp[index]) {
        printk("Bad kmalloc\n");
        return -ENOMEM;
      }

      if (gpio_request_one(i, GPIOF_OUT_INIT_LOW, NULL) < 0) {
        printk(KERN_ALERT "Error requesting GPIO %d\n", i);
        return -ENODEV;
      }
      raspi_gpio_devp[index]->dir = out;
      raspi_gpio_devp[index]->state = low;
      raspi_gpio_devp[index]->irq_perm = false;
      raspi_gpio_devp[index]->irq_flag = IRQF_TRIGGER_RISING;
      raspi_gpio_devp[index]->irq_counter = 0;
      raspi_gpio_devp[index]->cdev.owner = THIS_MODULE;

      spin_lock_init(&raspi_gpio_devp[index]->lock);

      cdev_init(&raspi_gpio_devp[index]->cdev, &raspi_gpio_fops);

      if ((ret = cdev_add( &raspi_gpio_devp[index]->cdev,
                          (first + i),
                          1))) {
        printk(KERN_ALERT "Error %d adding cdev\n", ret);
        for (i = 0; i < MAX_GPIO_NUMBER; i++) {
          if (is_valid_pin(i)) {
            device_destroy(raspi_gpio_class,
                            MKDEV(MAJOR(first),
                              MINOR(first) + i));
          }
        }
        class_destroy(raspi_gpio_class);
        unregister_chrdev_region(first, NUM_GPIO_PINS);

        return ret;
      }

      if (device_create(raspi_gpio_class,
                        NULL,
                        MKDEV(MAJOR(first), MINOR(first) + i),
                        NULL,
                        "raspiGpio%d", i) == NULL) {
        class_destroy(raspi_gpio_class);
        unregister_chrdev_region(first, NUM_GPIO_PINS);

        return -1;
      }
      index++;

    }
  }

  // Configure interrupt
  do_gettimeofday(&tv);
  epochMilli = (uint64_t)tv.tv_sec * (uint64_t)1000 + 
                (uint64_t)(tv.tv_usec/1000);

  printk("RaspberryPi GPIO driver Initialized\n");
  return 0;
}

/*
 * raspi_gpio_exit - Clean up GPIO device driver when unload
 * 
 * This function performs the following tasks:
 * Release major number
 * Release device nodes in /dev
 * Release per-device structure arrays
 * Detroy class in /sys
 * Set all GPIO pins to output, low level
 */
static void __exit raspi_gpio_exit(void)
{
  int i = 0;


  for (i=0; i<MAX_GPIO_NUMBER; i++) {
    if (is_valid_pin(i)) {
      device_destroy(raspi_gpio_class, MKDEV(MAJOR(first), MINOR(first)+i));
      gpio_direction_output(i, 0);
      gpio_free(i);
    }
  }


  for (i = 0; i < NUM_GPIO_PINS; i++){
    cdev_del(&(raspi_gpio_devp[i]->cdev));
    kfree(raspi_gpio_devp[i]);
  }

  class_destroy(raspi_gpio_class);
  unregister_chrdev_region(first, NUM_GPIO_PINS);

	printk(KERN_INFO "RaspberryPi GPIO driver remove\n");
}

MODULE_LICENSE("GPL");
module_init(raspi_gpio_init);
module_exit(raspi_gpio_exit);
