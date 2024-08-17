#如果编译成.ko的话就选obj-m,如果编译到内核就obj-y

# touchscreen目录下的Makefile中也需要添加以下修改
# obj-n 把gt9xx给屏蔽掉避免后面影响
# 			obj-n                               += gt9xx/
# obj-y 编译我们的my_touch
# 			obj-y                               += my_touch/

obj-y   += my_touch.o