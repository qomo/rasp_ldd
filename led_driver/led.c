#include <linux/init.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/io.h>

// include RPi harware specific constants
// GPIO_BASE SZ_4K
#include <mach/hardware.h>

#define LED_DRIVER_NAME   "led"

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


/* main function */

void hello_timer(unsigned long ptr)
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

static int hello_init(void)
{
    int result, i;

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

    printk(KERN_ALERT "Hello, LED world!\n");

    init_port();

    GPIO_SET_OUT(gpio_pin);

    init_timer(&led_timer);
    led_timer.function = hello_timer;
    led_timer.data = (unsigned long) &kbledstatus;
    led_timer.expires = jiffies + HZ;
    add_timer(&led_timer);

    return 0;

exit_rpi:
    return result;
}

void hello_exit(void)
{
    del_timer(&led_timer);
	printk(KERN_ALERT "goodbye world!\n");
}

MODULE_LICENSE("GPL");
module_init(hello_init);
module_exit(hello_exit);
