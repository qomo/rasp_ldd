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
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <asm/uaccess.h>		// for put_user

// include RPI hardware specific constants
#include <mach/hardware.h>

#define DHT11_DRIVER_NAME "dht11"
#define DEV_COUNT 1
#define RBUF_LEN 256
#define SUCCESS 0
#define BUF_LEN 80

// set GPIO pin g as input
#define GPIO_DIR_INPUT(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
// set GPIO pin g as output
#define GPIO_DIR_OUTPUT(g) *(gpio+((g)/10)) |= (1<<(((g)%10)*3))
// get logical value from gpio pin g
#define GPIO_READ_PIN(g) (*(gpio+13) & (1<<(g))) && 1
// sets bits which are 1 ignores bits which are 0
#define GPIO_SET_PIN(g) *(gpio+7) = 1<<g;
// clears bits which are 1 ignores bits which are 0
#define GPIO_CLEAR_PIN(g) *(gpio+10) = 1<<g;
// Clear GPIO interrupt on the pin we use
#define GPIO_INT_CLEAR(g) *(gpio+16) = (*(gpio+16) | (1<<g));
// GPREN0 GPIO Pin Rising Eedg Detect Enable/Disable
#define GPIO_INT_RISING(g,v) *(gpio+19) = v ? (*(gpio+19) | (1<<g)) : (*(gpio+19)^(1<<g))
// GPREN0 GPIO Pin Falling Edge Detect Enable/Disable
#define GPIO_INT_FALLING(g,v) *(gpio+22) = v ? (*(gpio+22) | (1<<g)) : (*(gpio+22)^(1<<g))



// module parameters
static int sense = 0;
static struct timeval lasttv = {0, 0};

static spinlock_t lock;

// Forward declarations
static int open_dht11(struct inode *, struct file *);
static int close_dht11(struct inode *,  struct file *);
static ssize_t read_dht11(struct file *, char *, size_t, loff_t *);
static void clear_interrupts(void);


// Global variables are declared as static, so are global within the file.
static int Device_Open = 0;	// Is device open? Used to prevent multiple access to device
static char msg[BUF_LEN];	// The msg the device will give when asked 
static char *msg_Ptr;	
static spinlock_t lock;
static unsigned int bitcount = 0;
static unsigned int bytecount = 0;
static unsigned int started = 0;		//Indicate if we have started a read or not
static unsigned char dht[5];			// For result bytes
static int format = 0;					// Default result format
static int gpio_pin = 22; 	//Default GPIO pin

module_param(format, int, S_IRUGO);
module_param(gpio_pin, int, S_IRUGO);

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
	.owner = THIS_MODULE,
	.read = read_dht11,
	.open = open_dht11,
	.release = close_dht11
};

// Possible valid GPIO pins
int valid_gpio_pins[] = {0, 1, 4, 8, 7, 9, 10, 11, 14, 15, 17, 18, 21, 22, 23, 24, 25};

volatile unsigned *gpio;

// IRQ handler - where the timing takes place
static irqreturn_t irq_handler(int i, void *blah, struct pt_regs *regs)
{
	struct timeval tv;
	long deltv;
	int data = 0;
	int signal;


	// use the GPIO signal level
	signal = GPIO_READ_PIN(gpio_pin);


	/* reset interrupt */
	GPIO_INT_CLEAR(gpio_pin);

	if(sense != -1){
		// get current time
		do_gettimeofday(&tv);

		// get time since last interrupt in microseconds
		deltv = tv.tv_sec - lasttv.tv_sec;

		data = (int) (deltv*1000000 + (tv.tv_usec - lasttv.tv_usec));
		lasttv = tv;		//Save last interrupt time

		// printk(KERN_INFO DHT11_DRIVER_NAME ": irq_handler()\t started=%d\t signal=%d \t data=%d\n", started, signal, data);

		if((signal == 1)&(data > 40))
		{
			started = 1;
			return IRQ_HANDLED;
		}
		if((signal == 0)&(started == 1))
		{
			if(data > 80)
				return IRQ_HANDLED;
			if(data < 15)
				return IRQ_HANDLED;
			if(data > 60) //55
				dht[bytecount] = dht[bytecount] | (0x80 >> bitcount);		// Add a 1 to the data byte

			// Uncomment to log bits and durations - may affect performance and not be accurate!
			printk("B:%d, d:%d, dt:%d\n", bytecount, bitcount, data);
			bitcount++;
			if(bitcount == 8)
			{
				bitcount = 0;
				bytecount++;
			}
			// if(bytecount == 5)
				// printk(KERN_INFO DHT11_DRIVER_NAME "Result: %d, %d, %d, %d,%d\n", dht[0], dht[1], dht[2], dht[3], dht[4], dht[5]);
		}
	}
	return IRQ_HANDLED;
}


static int setup_interrupts(void)
{
	int result;
	unsigned long flags;

	result = request_irq(gpio_to_irq(22), (irq_handler_t) irq_handler, 0, DHT11_DRIVER_NAME, (void*) gpio);

	switch (result) {
		case -EBUSY:
			printk(KERN_ERR DHT11_DRIVER_NAME ": IRQ %d is busy\n", gpio_to_irq(22));
			return -EBUSY;
		case -EINVAL:
			printk(KERN_ERR DHT11_DRIVER_NAME ": Bad irq number or handler\n");
			return -EINVAL;
		default:
			printk(KERN_INFO DHT11_DRIVER_NAME ": Interrupt %04x obtained\n", gpio_to_irq(22));
			break;
	}

	spin_lock_irqsave(&lock, flags);

	// GPREN0 GPIO Pin Rising Edge Detect Enable
	GPIO_INT_RISING(gpio_pin, 1);
	// GPFEN0 GPIO Pin Falling Edge Detect Enable
	GPIO_INT_FALLING(gpio_pin, 1);

	// clear interrupt flag
	GPIO_INT_CLEAR(gpio_pin);

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}

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


static int __init dht11_init(void)
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

static void __exit dht11_exit(void)
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
static int open_dht11(struct inode *inode, struct file *file)
{
	char result[3]; 			// To say if the result is trustworthy or not
	int retry = 0;

	if (Device_Open)
		return -EBUSY;

	// try_module_get(THIS_MODULE); 		// Increase use count (看起来这是个不用了的功能：http://stackoverflow.com/questions/1741415/linux-kernel-modules-when-to-use-try-module-get-module-put)

	Device_Open++;

	// Take data low for min 18mS to start up DHT11
	printk(KERN_INFO DHT11_DRIVER_NAME " Start setup (open_dht11)\n");

start_read:
	started = 0;
	bitcount = 0;
	bytecount = 0;
	dht[0] = 0;
	dht[1] = 0;
	dht[2] = 0;
	dht[3] = 0;
	dht[4] = 0;

	GPIO_DIR_OUTPUT(gpio_pin); 			// Set pin to output
	GPIO_SET_PIN(gpio_pin);				// Take pin high
	mdelay(250);
	GPIO_CLEAR_PIN(gpio_pin);			// Set low
	mdelay(20);							// DHT11 needs min 18mS to signal a startup
	GPIO_SET_PIN(gpio_pin);				// Take pin high
	udelay(40);							// Stay high for a bit before swapping to read mode
	GPIO_DIR_INPUT(gpio_pin);			// Change to read


	// Start timer to time pulse length
	do_gettimeofday(&lasttv);

	// Set up interrupts
	setup_interrupts();

	//Give the dht11 time to reply
	mdelay(10);


	// printk(KERN_INFO DHT11_DRIVER_NAME "Result: %d, %d, %d, %d,%d\n", dht[0], dht[1], dht[2], dht[3], dht[4], dht[5]);
	// Check if the read results are valid. If not then try again!
	if ((dht[0] + dht[1] + dht[2] + dht[3] == dht[4]) & (dht[4] > 0))
		sprintf(result, "OK");
	else {
		retry++;
		sprintf(result, "BAD");
		if(retry == 2)
			goto return_result;
		clear_interrupts();

		mdelay(2100);			// Can only read from sensor every 1 second so give it time to recover
		goto start_read;
	}

	// Return the result in various different formats
return_result:
	switch(format){
		case 0:
			sprintf(msg, "Values: %d, %d, %d, %d, %d, %s\n", dht[0], dht[1], dht[2], dht[3], dht[4], result);
			break;
		case 1:
			sprintf(msg, "%0X,%0X,%0X,%0X,%0X,%s\n", dht[0], dht[1], dht[2], dht[3], dht[4], result);
			break;
		case 2:
			sprintf(msg, "%02X%02X%02X%02X%02X%s\n", dht[0], dht[1], dht[2], dht[3], dht[4], result);
			break;
		case 3:
			sprintf(msg, "Temperature: %dC\nHumidity: %d%%\nResult:%s\n", dht[0], dht[2], result);
			break;
	}
	msg_Ptr = msg;

	return SUCCESS;
}

// Called when a process closes the device file.
static int close_dht11(struct inode *inode, struct file *file)
{
	// Decrement the usage count, or else once you opened the file, you'll never get get rid of the module.
	// module_put(THIS_MODULE);
	Device_Open--;

	clear_interrupts();

	printk(KERN_INFO DHT11_DRIVER_NAME ": Device release(close_dht11)\n");

	return 0;
}

// Clear the GPIO edge detect interrupts
static void clear_interrupts(void)
{
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);

	// GPREN0 GPIO Pin Rising Edge Detect Disable
	GPIO_INT_RISING(gpio_pin, 0);

	// GPFEN0 GPIO Pin Falling Edge Detect Disable
	GPIO_INT_FALLING(gpio_pin, 0);

	spin_unlock_irqrestore(&lock, flags);

	free_irq(gpio_to_irq(22), (void *) gpio);
}

// Called when a process, which already opened the dev file, attempts to read from it.
static ssize_t read_dht11(struct file *filp,	// see include/linux/fs.h
							char *buffer,		// buffer to fill with data
							size_t lenght,		// lenght of the buffer
							loff_t * offset)
{
	// Number of bytes actually written to the buffer
	int bytes_read = 0;

	// If we're at the end of the message, return 0 signifying end of file
	if (*msg_Ptr == 0)
		return 0;

	// Actually put the data into the buffer
	while(lenght && *msg_Ptr) {
		// The buffer is in the user data segment, not the kernel segment so "*" assignment won't work. We have to use
		// put_user which copies data from the kernel datasegment to the user data segment.
		put_user(*(msg_Ptr++), buffer++);

		lenght--;
		bytes_read++;
	}
	printk(KERN_INFO DHT11_DRIVER_NAME ": Call device_read()");

	// Return the number of bytes put into the buffer
	return bytes_read;
}
MODULE_DESCRIPTION("DHT11 temperature/humidity sensor driver for Raspberry Pi GPIO");
MODULE_LICENSE("GPL");
module_init(dht11_init);
module_exit(dht11_exit);
	