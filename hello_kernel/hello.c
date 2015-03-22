#include <linux/module.h>
#include <linux/kernel.h>
int hello_init(void)
{
	printk(KERN_ALERT "hello world!\n");
	printk(KERN_INFO "%s\n", __func__);
	return 0;
}

void hello_exit(void)
{
	printk(KERN_ALERT "goodbye world!\n");
	printk(KERN_INFO "%s\n", __func__);
}

MODULE_LICENSE("GPL");
module_init(hello_init);
module_exit(hello_exit);
