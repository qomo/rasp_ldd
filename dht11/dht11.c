/* dht11.c
 *
 * dht11 - Device driver for reading values from DHT11 temperature and humidity sensor.
 *
 * By default the DHT11 is connected to GPIO pin 22 
 * The Major version default is 80 but can be set via the command line.
 * Command line parameters: 
 * 			gpio_pin=X - a valid GPIO pin value.
 * 			driverno=X - value for major driver number
 * 			format=X   - format of the output from the sensor
 *
 * Usage:
 * 		Load driver: insmod ./dht11.ko <optional variables>
 * 					i.e.	insmod ./dht11.ko gpio_pin=2 format=3
 * 		Set up device file to read from (i.e.):
 * 					mknod /dev/dht11 c 80 0
 * 					mknod /dev/myfile c <driverno> 0 	
 * 						- to set the output to your own file and driver number
 *		To read the values from the sensor: cat /dev/dht11
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>

// include RPI hardware specific constants
#include <mach/hardware.h>

#define DHT11_DRIVER_NAME "dht11"
#define DEV_COUNT 1
#define RBUF_LEN 256
#define SUCCESS 0
#define BUF_LEN 80


// module parameters
static int sense = 0;
static struct timeval lasttv = {0, 0};

static spinlock_t lock;

// Forward declarations
static int read_dht11(struct inode *, struct file *);
static int close_dht11(struct inode *,  struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static void clear_interrupts(void);


// Global variables are declared as static, so are global within the file.
static int gpio_pin = 0; 	//Default GPIO pin

// 定义设备模型
// struct dht11_dev {
// 	struct cdev cdev;
// 	struct mutex mutex
// }
// 声明设备号
static dev_t dev_number;
static int dev_major;

struct proc_dir_entry *entry;

//Operations that can be performed on the device
static struct file_operations fops = {
	.read = device_read,
	.open = read_dht11,
	.release = close_dht11
};

int valid_gpio_pins[] = {0, 1, 4, 8, 7, 9, 10, 11, 14, 15, 17, 18, 21, 22, 23, 24, 25};

volatile unsigned *gpio;


// Initialise GPIO memory
static int init_port(void)
{
	// reserve GPIO memory region.
	if (request_mem_region(GPIO_BASE, SZ_4K, DHT11_DRIVER_NAME)) {
		printk(KERN_ERR DHT11_DRIVER_NAME ": unable to obtain GPIO I/O memory address\n");
		return -EBUSY;
	}

	// remap the GPIO memory
	if ((gpio = ioremap_nocache(GPIO_BASE, SZ_4K)) == NULL) {
		printk(KERN_ERR DHT11_DRIVER_NAME ": failed to map GPIO I/O memory!\n");
		return -EBUSY;
	}

	return 0;
}


int dht11_init(void)
{
	int result;
	int i; 

	// check for valid gpio pin number
	result = 0;
	for(i = 0; (i < ARRAY_SIZE(valid_gpio_pins)) && (result != 1); i++) {
		if(gpio_pin == valid_gpio_pins[i])
			result++;
	}

	if (result != 1) {
		result = -EINVAL;
		printk(KERN_ERR DHT11_DRIVER_NAME ": invalid GPIO pin specified!\n");
		goto exit_rpi;
	}

	result = alloc_chrdev_region(&dev_number, 0, DEV_COUNT, DHT11_DRIVER_NAME);

	if (result < 0) {
		printk(KERN_ALERT DHT11_DRIVER_NAME ": Registering dht11 driver failed with %d\n", result);
		return result;
	}

	dev_major = MAJOR(dev_number);

	printk(KERN_INFO DHT11_DRIVER_NAME ": driver registered!\n");

	entry = proc_create(DHT11_DRIVER_NAME, 0, NULL, &fops);

	result = init_port();
	if (result < 0)
		goto exit_rpi;

	return 0;

exit_rpi:
	return result;
}

void dht11_exit(void)
{
	// release mapped memory and allocated region
	if (gpio != NULL) {
		iounmap(gpio);
		release_mem_region(GPIO_BASE, SZ_4K);
		printk(DHT11_DRIVER_NAME ": cleaned up resourses\n");
	}

	remove_proc_entry(DHT11_DRIVER_NAME, NULL);

	// Unregister the driver
	unregister_chrdev(dev_number, DEV_COUNT);
	printk(DHT11_DRIVER_NAME ": cleaned up module\n");
}


// Called when a process wants to read the dht11 "cat /dev/dht11"
static int read_dht11(struct inode *inode, struct file *file)
{
	printk(KERN_INFO DHT11_DRIVER_NAME ": Call read_dht11()");
}

// Called when a process closes the device file.
static int close_dht11(struct inode *inode, struct file *file)
{
	printk(KERN_INFO DHT11_DRIVER_NAME ": Call close_dht11()");
}

// Called when a process, which already opened the dev file, attempts to read from it.
static ssize_t device_read(struct file *filp,	// see include/linux/fs.h
							char *buffer,		// buffer to fill with data
							size_t lenght,		// length of the buffer
							loff_t * offset)
{
	printk(KERN_INFO DHT11_DRIVER_NAME ": Call device_read()");
}
MODULE_DESCRIPTION("DHT11 temperature/humidity sensor driver for Raspberry Pi GPIO");
MODULE_LICENSE("GPL");
module_init(dht11_init);
module_exit(dht11_exit);
	