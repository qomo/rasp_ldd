#include <linux/module.h>
#include <linux/kernel.h>
int dht11_init(void)
{
	printk(KERN_ALERT "hello world!\n");
	printk(KERN_INFO "%s\n", __func__);
	return 0;
}

void dht11_exit(void)
{
	printk(KERN_ALERT "goodbye world!\n");
	printk(KERN_INFO "%s\n", __func__);
}

MODULE_LICENSE("GPL");
module_init(dht11_init);
module_exit(dht11_exit);
