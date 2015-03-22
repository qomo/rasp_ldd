# rasp_ldd

## 环境配置
基金会网站下载的镜像不带有内核头文件

需要安装www.raspbian.org提供的内核与头文件，具体过程看
> http://qomo.sinaapp.com/%E6%A0%91%E8%8E%93%E6%B4%BEget-linux-headers/

## hello_kernel
- 目录－－hello_kernel
- insmod之后不能马上看到hello world输出，只有rmmod之后才能通过dmesg看到hello world和goodbye输出消息，跟之前在pc里的不太一样，不知为何
- 上面的问题已经解决，因为原来的代码输出"hello world!"，没有换行，不会立即输出，改为"hello world!\n"后OK
