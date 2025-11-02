# kernel module name
obj-m += lkm_pcf8574_lcd.o
ccflags-y += -O0 -g
lkm_pcf8574_lcd-objs += lkm_pcf8574_lcd_platform_core.o
