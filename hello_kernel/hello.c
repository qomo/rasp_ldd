#include <linux/module.h>
#include <linux/kernel.h>
int hello_init(void)
{
	printk(KERN_ALERT "hello world!\n");
	return 0;
}

void hello_exit(void)
{
	printk(KERN_ALERT "goodbye world!\n");
}

MODULE_LICENSE("GPL");
module_init(hello_init);
module_exit(hello_exit);
