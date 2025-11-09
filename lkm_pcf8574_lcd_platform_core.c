#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/device/class.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>

#define FONT_5x10		1
#define FONT_5x7		0
#define ROWS_2			1
#define ROW2_1			0
#define DISPLAY_SHIFT	1
#define CURSOR_SHIFT	0
#define RIGHT			1
#define LEFT			0
#define SHIFT			1
#define NOT_SHIFT		0
#define INCREMENT		1
#define DECREMENT		0
#define DISPLAY_LINE_8D 1
#define DISPLAY_LINE_4D 0
#define DISPLAY_ON		1
#define DISPLAY_OFF		0
#define CURSOR_ON		1
#define CURSOR_OFF		0
#define BLINK_ON		1
#define BLINK_OFF		0

#define SET_RS			1
#define NO_RS			0

#define DEVICE_NAME		"lcd1602"
#define DEVICE_CLASS 	"lcd1602_class"
#define MAX_DEVICES		10
#define NUM_ROWS		2
#define ROW_LENGTH		16
#define LCD_BUFFER_LEN	32 // 16 CHARS * 2 ROWS

struct lcd_shift_data {
	u_int8_t sc;
	u_int8_t rl;
} __attribute__((packed));

struct lcd_string_data {
    char data[LCD_BUFFER_LEN+1];
    size_t length;  // Actual string length (not including null)
} __attribute__((packed));

#define LCD_MAGIC 		  0x5A
#define LCD_CLEAR_SCREEN  _IO(LCD_MAGIC, 0)
#define LCD_BACKLIGHT_ON  _IO(LCD_MAGIC, 1)
#define LCD_BACKLIGHT_OFF _IO(LCD_MAGIC, 2)
#define LCD_WR_VALUE      _IOW(LCD_MAGIC, 3, int)
#define LCD_RD_VALUE      _IOR(LCD_MAGIC, 4, struct lcd_string_data)
#define LCD_SHIFT		  _IOW(LCD_MAGIC, 5, int)
#define LCD_CURSOR_RETURN _IO(LCD_MAGIC, 6)

#define TRUE 	1
#define FALSE	0

//formatter for pr_xxxx
#define pr_fmt(fmt) "lcd1602: " fmt

static const struct of_device_id lcd_of_match[] = {
    { .compatible = "mycompany,lcd1602" },
    { }
};
MODULE_DEVICE_TABLE(of, lcd_of_match);

struct pcf8574_gpios {
	struct gpio_desc *d4;
	struct gpio_desc *d5;
	struct gpio_desc *d6;
	struct gpio_desc *d7;
	struct gpio_desc *en;
	struct gpio_desc *rw;
	struct gpio_desc *rs;
	struct gpio_desc *bl;
};

typedef struct lcd_device{
	dev_t dev_num;
	char dev_name[32];
	struct cdev cdev;	//it has to be value not poitner because of container_of()
	struct class *lcd_class;
	struct device *device;
	struct pcf8574_gpios gpios;
	//char data[LCD_BUFFER_LEN];
	u8 data[LCD_BUFFER_LEN];
	u8 data_len;
	u8 row;
	u8 col;
} lcd1602_dev;

struct lcd1602_drv_private_data {
	int total_devices;
	lcd1602_dev *dev;
};

struct lcd1602_drv_private_data lcd_drv_data;

// set enable bit to TRUE and FALSE so pcf8574 can set data
static void strobe(lcd1602_dev *dev)
{
	// set enable bit to TRUE
	gpiod_set_value_cansleep(dev->gpios.en, TRUE);
	udelay(1);

	// set enable bit to FALSE
	gpiod_set_value_cansleep(dev->gpios.en, FALSE);
	udelay(50);
}

/* returns pos of the newline  in string */
static int check_new_line(const char* string, size_t len) {
    char *e = strnchr(string, len, '\n');
    if (e == NULL) {
        return -1;
    }
    return (int)(e - string);
}

static void write_nibble(lcd1602_dev *dev, u8 nibble, bool rs)
{
	gpiod_set_value_cansleep(dev->gpios.rs, rs ? TRUE : FALSE);

	gpiod_set_value_cansleep(dev->gpios.d4, (nibble >> 0) & 0x01);
	gpiod_set_value_cansleep(dev->gpios.d5, (nibble >> 1) & 0x01);
	gpiod_set_value_cansleep(dev->gpios.d6, (nibble >> 2) & 0x01);
	gpiod_set_value_cansleep(dev->gpios.d7, (nibble >> 3) & 0x01);

    strobe(dev);
}

static void write_byte(lcd1602_dev *lcd_device, unsigned data, bool rs)
{
	// write high nibble
	write_nibble(lcd_device, ((data & 0xF0) >> 4), rs);
	// write low nibble
	write_nibble(lcd_device, (data & 0x0F), rs);
}

// Clear display
static void lcd_clear_screen(lcd1602_dev *dev)
{
	pr_info("lcd screen clear\n");
	dev->row = 0;
	dev->col = 0;
	dev->data_len = 0;
	memset(dev->data, 0, LCD_BUFFER_LEN);
	write_byte(dev, 0x01, false);
	msleep(2);
}

static void lcd_cursor_return(lcd1602_dev *dev) {
	pr_info("lcd cursor return\n");
	write_byte(dev, 0x02, false);
	msleep(2);
}

static void lcd_shift(lcd1602_dev *dev, u8 sc, u8 rl) {
	pr_info("lcd shift\n");

	if(sc == DISPLAY_SHIFT && rl == RIGHT)
		write_byte(dev, 0x1C, false);
	else if(sc == DISPLAY_SHIFT && rl == LEFT)
		write_byte(dev, 0x18, false);
	else if(sc == CURSOR_SHIFT && rl == RIGHT)
		write_byte(dev, 0x14, false);
	else if(sc == CURSOR_SHIFT && rl == LEFT)
		write_byte(dev, 0x10, false);

	msleep(2);
}

static void lcd_backlight_on(lcd1602_dev *dev)
{
	pr_info("lcd backlight on\n");
	gpiod_set_value_cansleep(dev->gpios.bl, TRUE);
}

static void lcd_backlight_off(lcd1602_dev *dev)
{
	pr_info("lcd backlight off\n");
	gpiod_set_value_cansleep(dev->gpios.bl, FALSE);
}

static void lcd_set_cursor(lcd1602_dev *lcd_device)
{
	switch (lcd_device->row)
	{
		case 0:
		write_byte(lcd_device, 0x80, false);
			break;
		case 1:
		write_byte(lcd_device, 0xC0, false);
			break;
	}
}

static void initialize_lcd(lcd1602_dev *dev)
{
	/* Step 0: wait for power to stabilize */
	msleep(20);
	pr_info("4 bit Initialization started \n");
	msleep(100);
	char binary[9];
	memset(binary, 0, 9);

	/* Steps 1–3: send 0x3 three times in 8-bit compatibility mode */
	write_nibble(dev, 0x03, FALSE);
	msleep(5);
	write_nibble(dev, 0x03, FALSE);
	udelay(200);
	write_nibble(dev, 0x03, FALSE);
	udelay(200);

	/* Step 4: set 4-bit interface */
	write_nibble(dev, 0x02, FALSE);
	udelay(200);

	/* Function set: 4-bit, 2 lines, 5x8 font (0x28)*/
	write_byte(dev, 0x28, false);
	udelay(200);

	/* Display OFF (0x08)*/
	write_byte(dev, 0x08, false);
	udelay(200);

	/* Clear display */
	lcd_clear_screen(dev);
	udelay(200);

	/* Entry mode: increment cursor, no shift (0x06) */
	write_byte(dev, 0x06, false);
	udelay(200);

	/* Display ON, cursor OFF, blink OFF (0x0C)*/
	write_byte(dev, 0x0C, false);
	/* Display ON, cursor ON, blink ON (0x0F)*/
	write_byte(dev, 0x0F, false);
	udelay(200);

	pr_info("4 bit initialization done\n");
}

static void lcd_write(lcd1602_dev *lcd_device, const u8 *buffer, const size_t len)
{
	size_t i;
	size_t start = lcd_device->data_len;

	for (i = start; i < start + len; ++i){
		// check if lcd screen is full
		if(lcd_device->data_len >= LCD_BUFFER_LEN) {
			pr_info("Data is full lcd_device->data_len=%d\n", lcd_device->data_len);
			return;
		}

		// do not attempt to write newline
		// just jump to next row
		if(buffer[lcd_device->data_len-start] == '\n'){
			lcd_device->row++;
			lcd_device->data[i] = buffer[lcd_device->data_len-start];
			lcd_device->data_len++;
			pr_info("newline detected in the buffer, moving to the row %d\n", lcd_device->row);
			lcd_set_cursor(lcd_device);
			continue;
		} else {
			// if there is no more place in
			// first row continue to second
			if(lcd_device->row == 0 && lcd_device->data_len>ROW_LENGTH-1){
				lcd_device->row++;
				pr_info("input is too long, moving to the next row\n");
				lcd_set_cursor(lcd_device);
			}
			lcd_device->data[i] = buffer[lcd_device->data_len-start];
			lcd_device->data_len++;

			write_byte(lcd_device, lcd_device->data[i], SET_RS);
			continue;
		}
	}
}

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static long device_ioctl(struct file *, unsigned int, unsigned long);

/* Talk to 8-bit I/O expander */
static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl
};

/* This function will be called when we write IOCTL on the Device file */
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	pr_info("received command %d\n", cmd);
	switch(cmd) {
		case LCD_WR_VALUE:
			char user_value[LCD_BUFFER_LEN];
			memset(user_value, 0, LCD_BUFFER_LEN);
			if( copy_from_user(&user_value ,(char*) arg, sizeof(user_value)))
				pr_err("Data Write : Err!\n");

			if (strlen(lcd_drv_data.dev->data)>0) {
				// concat only as much as LCD can take
				if(lcd_drv_data.dev->data_len + strlen(user_value) > LCD_BUFFER_LEN)
					strncat(lcd_drv_data.dev->data, user_value, lcd_drv_data.dev->data_len + strlen(user_value) - LCD_BUFFER_LEN);
				else
					strncat(lcd_drv_data.dev->data, user_value, strlen(user_value));
			}
			else
				memcpy(lcd_drv_data.dev->data, user_value, strlen(user_value));
			pr_info("Value to write = %s len = %d \n", lcd_drv_data.dev->data, strlen(lcd_drv_data.dev->data));
			lcd_write(lcd_drv_data.dev, user_value, strlen(user_value));

			break;
		case LCD_RD_VALUE:
			struct lcd_string_data lcd_data;
			memset(&lcd_data, 0, sizeof(struct lcd_string_data));

			memcpy(lcd_data.data, lcd_drv_data.dev->data, lcd_drv_data.dev->data_len);
			lcd_data.length = lcd_drv_data.dev->data_len;

			if(copy_to_user((struct lcd_string_data __user *)arg, &lcd_data, sizeof(struct lcd_string_data)))
				pr_err("Data Read : Err!\n");

			break;
		case LCD_CLEAR_SCREEN:
			lcd_clear_screen(lcd_drv_data.dev);
			break;
		case LCD_BACKLIGHT_OFF:
			lcd_backlight_off(lcd_drv_data.dev);
			break;
		case LCD_BACKLIGHT_ON:
			lcd_backlight_on(lcd_drv_data.dev);
			break;
		case LCD_SHIFT:
			struct lcd_shift_data data;
			if( copy_from_user(&data ,(struct lcd_shift_data __user *)arg, sizeof(data)))
				pr_err("Data Write : Err!\n");

			pr_info("Dec: sc=%u, rl=%u\n", data.sc, data.rl);

			lcd_shift(lcd_drv_data.dev, data.sc, data.rl);
			break;
		case LCD_CURSOR_RETURN:
			lcd_cursor_return(lcd_drv_data.dev);
			break;
		default:
			pr_info("Default\n");
			break;
	}
	return 0;
}

/* Called when a process tries to open the device file */
static int device_open(struct inode *inode, struct file *file)
{
	pr_info("device opened\n");
	lcd1602_dev *lcd_device;

	if (!inode->i_cdev) {
        pr_err("device_open: inode->i_cdev is NULL!\n");
        return -ENODEV;
	}
	// Get pointer to containing structure
	lcd_device = container_of((struct cdev *)inode->i_cdev, struct lcd_device, cdev);

	if (!lcd_device) {
		pr_err("device_open: container_of failed!\n");
        return -ENODEV;
	}

	file->private_data = lcd_device;

	return 0;
}

/* Called when a process closes the device file */
static int device_release(struct inode *inode, struct file *file)
{
	pr_info("releasing device\n");
	return 0;
}

/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t device_read(struct file *filp,
							char *buffer,    /* The buffer to fill with data */
							size_t length,   /* The length of the buffer     */
							loff_t *offset)  /* Our offset in the file       */
{
	pr_info("inside device_read\n");
	return 0;
}

/* Called when a process writes to dev file: echo "hi" > /dev/<dev_name> */
static ssize_t device_write(struct file *filp,
							const char *buff,
							size_t len,
							loff_t *off)
{
	lcd1602_dev *lcd_device;
	int next_row = 0;
	int length_to_copy;
	int rest;
	const char *data;
	char *buff_start, *buff_end;
	int newline_pos;

	lcd_device = filp->private_data;
	rest = copy_from_user(buff_start, buff, length_to_copy);

	// write to display
	pr_info("writing %s to screen \n", buff);
	lcd_write(lcd_device, buff, len-1);

	*off = strlen(lcd_device->data);

	return len;
}

static int lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lcd_device *dev_data;

	int status;
	int ret;

	dev_data = devm_kzalloc(dev, sizeof(struct lcd_device), GFP_KERNEL);
	if(!dev_data){
		dev_err(dev, "Can not allocate memory!\n");
		return -ENOMEM;
	}

	pr_info("Registering the character device...");

	// Allocate device name
	char *device = devm_kzalloc(dev, 32, GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	sprintf(device, "%s_%d", DEVICE_NAME, lcd_drv_data.total_devices);

	ret = alloc_chrdev_region(&dev_data->dev_num, 0, 1, device);
	if (ret < 0) {
		pr_err("Failed to allocate chrdev region\n");
		return ret;
	}

	// Initialize cdev structure and add it
	cdev_init(&dev_data->cdev, &fops);
	dev_data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev_data->cdev, dev_data->dev_num, 1);
	if (ret < 0) {
		unregister_chrdev_region(dev_data->dev_num, 1);
		pr_err("Failed to add cdev\n");
		return ret;
	}

	pr_info("Registered the character device, major=%d, minor=%d\n", 
		MAJOR(dev_data->dev_num), MINOR(dev_data->dev_num));

	pr_info("Creating the device class...");

	// Allocate class name
	char *class_name = devm_kzalloc(dev, 32, GFP_KERNEL);
	if (!class_name) {
		ret = -ENOMEM;
		goto fail_class;
	}

	sprintf(class_name, "%s_%d", DEVICE_CLASS, lcd_drv_data.total_devices);

	dev_data->lcd_class = class_create(class_name);
	if (IS_ERR(dev_data->lcd_class)){
		pr_err("Failed to create the device class...\n");
		ret = PTR_ERR(dev_data->lcd_class);
		goto fail_class;
	}

	pr_info("Creating the device...");
	strncpy(dev_data->dev_name, device, sizeof(dev_data->dev_name) - 1);

	dev_data->device = device_create(dev_data->lcd_class, NULL, dev_data->dev_num, NULL, device);
	if (IS_ERR(dev_data->device)) {
		pr_err("Failed to create the device...\n");
		ret = PTR_ERR(dev_data->device);
		goto fail_device;
	}

	pr_info("Created device with name %s\n", dev_data->dev_name);

	lcd_drv_data.dev = dev_data;
	lcd_drv_data.total_devices++;

	platform_set_drvdata(pdev, dev_data);

	pr_info("Initializing pcf8574 gpios ... \n");
	lcd_drv_data.dev->gpios.d4 = devm_gpiod_get(dev, "d4", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.d4))
		return PTR_ERR(lcd_drv_data.dev->gpios.d4);

	lcd_drv_data.dev->gpios.d5 = devm_gpiod_get(dev, "d5", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.d5))
		return PTR_ERR(lcd_drv_data.dev->gpios.d5);

	lcd_drv_data.dev->gpios.d6 = devm_gpiod_get(dev, "d6", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.d6))
		return PTR_ERR(lcd_drv_data.dev->gpios.d6);

	lcd_drv_data.dev->gpios.d7 = devm_gpiod_get(dev, "d7", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.d7))
		return PTR_ERR(lcd_drv_data.dev->gpios.d7);

	lcd_drv_data.dev->gpios.en = devm_gpiod_get(dev, "en", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.en))
		return PTR_ERR(lcd_drv_data.dev->gpios.en);

	lcd_drv_data.dev->gpios.rw = devm_gpiod_get(dev, "rw", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.rw))
		return PTR_ERR(lcd_drv_data.dev->gpios.rw);

	lcd_drv_data.dev->gpios.rs = devm_gpiod_get(dev, "rs", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.rs))
		return PTR_ERR(lcd_drv_data.dev->gpios.rs);

	lcd_drv_data.dev->gpios.bl = devm_gpiod_get(dev, "bl", GPIOD_OUT_LOW);
	if (IS_ERR(lcd_drv_data.dev->gpios.bl))
		return PTR_ERR(lcd_drv_data.dev->gpios.bl);

	//set backlight on
	lcd_backlight_on(lcd_drv_data.dev);
	initialize_lcd(lcd_drv_data.dev);

	return 0;

fail_device:
	device_destroy(dev_data->lcd_class, dev_data->dev_num);
	class_destroy(dev_data->lcd_class);
fail_class:
	cdev_del(&dev_data->cdev);
	unregister_chrdev_region(dev_data->dev_num, 1);
	pr_err("Probe failed with error %d\n", status);
	return ret;
}

static int lcd_remove(struct platform_device *pdev)
{
    struct lcd_device *dev = platform_get_drvdata(pdev);

	// Rest of your cleanup (device_destroy, class_destroy, etc.)
	if (dev) {
		// Turn off backlight
		lcd_backlight_off(dev);
			// Clear display
		lcd_clear_screen(dev);

		pr_info("Module unloading, total_devices %d\n", lcd_drv_data.total_devices);

		pr_info("Destroying device major=%d, minor=%d class ptr=%p\n", MAJOR(dev->dev_num), MINOR(dev->dev_num), dev->lcd_class);
		device_destroy(dev->lcd_class, dev->dev_num);
		pr_info("Destroying device class...\n");
		class_destroy(dev->lcd_class);
		// Unregister the device
		pr_info("Deleting cdev\n");
		cdev_del(&dev->cdev);
		pr_info("Unregistering character device major=%d, minor=%d ...\n", MAJOR(dev->dev_num), MINOR(dev->dev_num));
		unregister_chrdev_region(dev->dev_num, 1);
	}

	return 0;
}

static struct platform_driver lcd_driver = {
	.driver = {
		.name = "lcd1602",
		.of_match_table = of_match_ptr(lcd_of_match),
	},
	.probe = lcd_probe,
	.remove = lcd_remove,
};

module_platform_driver(lcd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("me");

