# rasp_ldd

你还可以参考我学习《LDD3》时建立的另一个杂七杂八的项目：
> https://github.com/qomo/LDD_learning

## 环境配置
基金会网站下载的镜像不带有内核头文件

需要安装www.raspbian.org提供的内核与头文件，具体过程看
> http://qomo.sinaapp.com/%E6%A0%91%E8%8E%93%E6%B4%BEget-linux-headers/

## hello_kernel
- 目录－－hello_kernel
- insmod之后不能马上看到hello world输出，只有rmmod之后才能通过dmesg看到hello world和goodbye输出消息，跟之前在pc里的不太一样，不知为何
- 上面的问题已经解决，因为原来的代码输出"hello world!"，没有换行，不会立即输出，改为"hello world!\n"后OK

## led_driver
**参考：**

> http://www.slideshare.net/raspberrypi-tw/write-adevicedriveronraspberrypihowto

> https://www.youtube.com/watch?v=RlNPtBEZRkY

这是一个最基本的LED驱动程序，让LED闪烁

第一个链接是一个关于树莓派驱动程序的PPT，它里面的例子是一个温湿度传感器的驱动，告诉我准确的控制GPIO的方法

第二个链接是YouTube上关于树莓派LED驱动的视频，按照视频里的方法控制GPIO会导致问题，但它提供了这个驱动程序的结构。

**涉及到的知识：**
- 内核定时器 －－ `struct timer_list`
- 《LDD》第九章 －－ 使用I/O内存

**接下来希望改进这个LED驱动，增加sysfs文件系统对LED闪烁频率的控制功能**


## led_driver2
暂且先增加一个字符设备驱动程序，它的功能很简单，就是通过设备节点传入一个闪烁频率的参数，用于控制LED灯的闪烁频率。

如何通过设备节点传入一个整数？

参考了一个带有整数属性的Android字符设备驱动程序：
> http://blog.csdn.net/liuhaoyutz/article/details/8500300

还有一些关于字符设备驱动程序的基础知识,
可以参考《Linux设备驱动程序》一书和我学习这本书的记录：
> https://github.com/qomo/LDD_learning

使用这个例子，需要自行在/dev/目录下创建设备文件:

`mknod /dev/led c 245 0`

然后就可以通过

`echo 10 > /dev/led`

传入频率参数了


## led_driver3
做了一个很简单的事情，就是增加一个／proc文件接口，用以控制led闪烁频率

led_entry 是`proc_dir_entry`类型指针
struct proc_dir_entry *entry;

添加proc文件接口的函数是

`led_entry = proc_create("led_freq", 0, NULL, &led_fops)`

删除proc文件接口的函数是

`remove_proc_entry("led_freq", NULL)`

参考：
- 《LDD3》第四章
- https://www.linux.com/learn/linux-training/37985-the-kernel-newbie-corner-kernel-debugging-using-proc-qsequenceq-files-part-1
