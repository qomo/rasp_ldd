# rasp_ldd

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
- 

**接下来希望改进这个LED驱动，增加sysfs文件系统对LED闪烁频率的控制功能**
