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
#include <asm/uaccess.h>

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
static int raspi_gpio_open(struct inode *inode, struct file *filp);
static int raspi_gpio_release(struct inode *inode, struct file *filp);
static ssize_t raspi_gpio_read(struct file *filp,
                              char *buf,
                              size_t count,
                              loff_t *f_pos);
static ssize_t raspi_gpio_write(struct file *filp,
                               const char *buf,
                               size_t count,
                               loff_t *f_pos);

/* File operation structure */
static struct file_operations raspi_gpio_fops = {
    .owner  = THIS_MODULE,
    .open   = raspi_gpio_open,
    .release= raspi_gpio_release,
    .read   = raspi_gpio_read,
    .write  = raspi_gpio_write,
};

/* Forward declaration of functions */
static int raspi_gpio_init(void);
static void raspi_gpio_exit(void);
unsigned int millis(void);
static irqreturn_t irq_handler(int irq, void *arg);

/* Global varibles for GPIO driver */
struct raspi_gpio_dev *raspi_gpio_devp[NUM_GPIO_PINS];
static dev_t first;
static struct class *raspi_gpio_class;
static unsigned int last_interrupt_time = 0;
static uint64_t epochMilli;


/* 
 * millis - Get current time
 * 
 * This function returns current time in ms. It is primarily used for
 * debouncing
 */
unsigned int millis(void)
{
  struct timeval timeval;
  uint64_t timeNow;

  do_gettimeofday(&timeval);
  timeNow = (uint64_t) timeval.tv_sec * (uint64_t)1000 +
            (uint64_t)(timeval.tv_usec/1000);

  return (uint32_t)(timeNow - epochMilli);
}

/*
 * irq_handler - Interrupt request handler for GPIO pin
 *
 * This feature is pretty experiment, so more work needs to
 * be done to make the feature useful for application
 */
static irqreturn_t irq_handler(int irq, void *arg)
{
  unsigned long flags;
  unsigned int interrupt_time = millis();

  if(interrupt_time - last_interrupt_time < 200){
    printk(KERN_NOTICE "Ignored Interrupt [%d]\n", irq);
    return IRQ_HANDLED;
  }
  last_interrupt_time = interrupt_time;

  local_irq_save(flags);
  printk(KERN_NOTICE "Interrupt [%d] was triggered\n", irq);
  local_irq_restore(flags);

  return IRQ_HANDLED;
}

/*
 * raspi_gpio_open - Open GPIO device node in /dev
 *
 * This function allocates GPIO interrupt resource when requested
 * on the condition that interrupt flag is enabled and pin direction
 * set to input, then allow the specified GPIO pin to set interrupt.
 */
static int raspi_gpio_open(struct inode *inode, struct file *filp)
{
  struct raspi_gpio_dev *raspi_gpio_devp;
  unsigned int gpio;
  int err, irq;
  unsigned long flags;

  gpio = iminor(inode);
  printk(KERN_INFO "GPIO[%d] opened\n", gpio);
  raspi_gpio_devp = container_of(inode->i_cdev,
                                struct raspi_gpio_dev,
                                cdev);

  if((raspi_gpio_devp->irq_perm == true) &&
      (raspi_gpio_devp->dir == in)) {
    if((raspi_gpio_devp->irq_counter++ == 0)){   // about irq_counter, see P261 of <<LDD3>>
      irq = gpio_to_irq(gpio);
      if(raspi_gpio_devp->irq_flag == IRQF_TRIGGER_RISING){
        spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
        err = request_irq(irq,
                          irq_handler,
                          IRQF_SHARED | IRQF_TRIGGER_RISING,
                          INTERRUPT_DEVICE_NAME,
                          raspi_gpio_devp);
        printk(KERN_INFO "interrupt requested\n");
        spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
      } else {
        spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
        err = request_irq(irq,
                          irq_handler,
                          IRQF_SHARED | IRQF_TRIGGER_FALLING,
                          INTERRUPT_DEVICE_NAME,
                          raspi_gpio_devp);
        printk(KERN_INFO "interrupt requested\n");
        spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
      }
      if(err != 0) {
        printk(KERN_ERR "unable to claim irq: %d, error %d\n", irq, err);
        return err;
      }
    }
  } 
  filp->private_data = raspi_gpio_devp;
  return 0;
}

/*
 * raspi_gpio_release - Release GPIO pin
 *
 * This functions releases GPIO interrupt resource when the device is
 * last closed. When requested to disable interrupt, it release GPIO
 * interrupt resource regardless of how many devices are using
 * interrupt.
 */
static int raspi_gpio_release(struct inode *inode, struct file *filp)
{
  unsigned int gpio;
  struct raspi_gpio_dev *raspi_gpio_devp;

  raspi_gpio_devp = container_of(inode->i_cdev,
                                struct raspi_gpio_dev,
                                cdev);
  gpio = iminor(inode);
  printk(KERN_INFO "Closing GPIO %d\n", gpio);

  spin_lock(&raspi_gpio_devp->lock);
  if(raspi_gpio_devp->irq_perm == true){
    if(raspi_gpio_devp->irq_counter > 0){
      raspi_gpio_devp->irq_counter--;
      if(raspi_gpio_devp->irq_counter == 0){
        printk(KERN_INFO "interrupt on gpio[%d] released\n", gpio);
        free_irq(gpio_to_irq(gpio), raspi_gpio_devp);
      }
    }
  }
  spin_unlock(&raspi_gpio_devp->lock);

  if(raspi_gpio_devp->irq_perm == false &&
    raspi_gpio_devp->irq_counter > 0){
    spin_lock(&raspi_gpio_devp->lock);
    free_irq(gpio_to_irq(gpio), raspi_gpio_devp);
    raspi_gpio_devp->irq_counter = 0;
    printk(KERN_INFO "interrupt on gpio[%d] disabled\n", gpio);
  }
  return 0;
}

/*
 * raspi_gpio_read - Read the state of GPIO pins
 *
 * This functions allows to read the logic state of input GPIO pins
 * and output GPIO pins. Since it multiple processes can read the
 * logic state of the GPIO, spin lock is not used here.
 */
static ssize_t raspi_gpio_read(struct file *filp,
                                char *buf,
                                size_t count,
                                loff_t *f_pos)
{
  unsigned int gpio;
  ssize_t retval;
  char byte;

  gpio = iminor(filp->f_path.dentry->d_inode);
  for(retval = 0; retval < count; ++retval){
    byte = '0' + gpio_get_value(gpio);
    if(put_user(byte, buf+retval))
      break;
  }
  return retval;
}

/*
 * raspi_gpio_write - Write to GPIO pin
 *
 * This function allows to set GPIO pin direction (input/out),
 * to set GPIO pin logic level (high/low), and to enable/diable
 * edge-triggered interrupt on a GPIO pin (rising/falling)
 * Set logic level (high/low) to an input GPIO pin is not permitted
 * The command set for setting GPIO pins is as follows
 * Command      Description
 * "out"        Set GPIO direction to output via gpio_direction_output
 * "in"         Set GPIO direction to input via gpio_direction_input
 * "1"          Set GPIO pin logic level to high
 * "0"          Set GPIO pin logic level to low
 * "rising"     Enable rising edge triger
 * "falling"    Enable falling edge triger
 * "disable-irq"  Disable interrupt on a GPIO pin 
 */
static ssize_t raspi_gpio_write(struct file *filp,
                                const char *buf,
                                size_t count,
                                loff_t *f_pos)
{
  unsigned int gpio, len=0, value=0;
  char kbuf[BUF_SIZE];
  struct raspi_gpio_dev *raspi_gpio_devp = filp->private_data;
  unsigned long flags;

  gpio = iminor(filp->f_path.dentry->d_inode);

  len = count < BUF_SIZE ? count-1 : BUF_SIZE-1;
  if(copy_from_user(kbuf, buf, len) != 0)
    return -EFAULT;
  kbuf[len] = '\0';

  printk(KERN_INFO "Request from user: %s\n", kbuf);

  // Check the content of kbuf and set GPIO pin accordingly
  if(strcmp(kbuf, "out") == 0){
    printk(KERN_ALERT "gpio[%d] direction set to output\n", gpio);
    if(raspi_gpio_devp->dir != out){
      spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
      gpio_direction_output(gpio, low);
      raspi_gpio_devp->dir = out;
      raspi_gpio_devp->state = low;
      spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
    }
  } else if(strcmp(kbuf, "in") == 0){
    if(raspi_gpio_devp->dir != in){
      printk(KERN_INFO "Set gpio[%d] direction: input\n", gpio);
      spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
      gpio_direction_input(gpio);
      raspi_gpio_devp->dir = in;
      spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
    }
  } else if((strcmp(kbuf, "1") == 0) || (strcmp(kbuf, "0") == 0)){
    sscanf(kbuf, "%d", &value);
    if(raspi_gpio_devp->dir == in){
      printk("Cannot set GPIO %d, direction: input\n", gpio);
      return -EPERM;
    }
    if(raspi_gpio_devp->dir == out){
      if(value>0){
        spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
        gpio_set_value(gpio, high);
        raspi_gpio_devp->state = high;
        spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
      } else {
        spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
        gpio_set_value(gpio, low);
        raspi_gpio_devp->state = low;
        spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
      }
    }
  } else if ((strcmp(kbuf, "rising") == 0) ||
              (strcmp(kbuf, "falling") == 0)) {
    spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
    gpio_direction_input(gpio);
    raspi_gpio_devp->dir = in;
    raspi_gpio_devp->irq_perm = true;
    if(strcmp(kbuf, "rising")==0)
      raspi_gpio_devp->irq_flag = IRQF_TRIGGER_RISING;
    else
      raspi_gpio_devp->irq_flag = IRQF_TRIGGER_FALLING;
    spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
  } else if ((strcmp(kbuf, "disable-irq") == 0)) {
    spin_lock_irqsave(&raspi_gpio_devp->lock, flags);
    raspi_gpio_devp->irq_perm = false;
    spin_unlock_irqrestore(&raspi_gpio_devp->lock, flags);
  } else {
    printk(KERN_ERR "Invalid value\n");
    return -EINVAL;
  }
  *f_pos += count;
  return count;
}


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
