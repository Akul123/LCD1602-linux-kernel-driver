import fcntl, os, struct, time
import ctypes

# Equivalent of C macros
def _IOC(dir, type, nr, size):
    IOC_NRBITS = 8
    IOC_TYPEBITS = 8
    IOC_SIZEBITS = 14
    IOC_DIRBITS = 2

    IOC_NRSHIFT = 0
    IOC_TYPESHIFT = IOC_NRSHIFT + IOC_NRBITS
    IOC_SIZESHIFT = IOC_TYPESHIFT + IOC_TYPEBITS
    IOC_DIRSHIFT = IOC_SIZESHIFT + IOC_SIZEBITS

    return ((dir << IOC_DIRSHIFT) |
            (ord(type) << IOC_TYPESHIFT) |
            (nr << IOC_NRSHIFT) |
            (size << IOC_SIZESHIFT))

# ioctl direction bits
IOC_NONE  = 0
IOC_WRITE = 1
IOC_READ  = 2

LCD_MAGIC = 0x5A  # from driver

def _IO(type, nr):
    return _IOC(IOC_NONE, chr(type), nr, 0)

def _IOW(type, nr, size):
    return _IOC(IOC_WRITE, chr(type), nr, size)

def _IOR(type, nr, size):
    return _IOC(IOC_READ, chr(type), nr, size)

LCD_BUFFER_SIZE = 32
class ShiftData(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("sc", ctypes.c_uint8),
        ("rl", ctypes.c_uint8),
    ]

class LcdData(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("data", ctypes.c_char * (LCD_BUFFER_SIZE + 1)), # +1 for '\0' in the end
        ("length", ctypes.c_size_t),
    ]

# Define ioctl commands exactly as in c
LCD_CLEAR_SCREEN  = _IO(LCD_MAGIC, 0)
LCD_BACKLIGHT_ON  = _IO(LCD_MAGIC, 1)
LCD_BACKLIGHT_OFF = _IO(LCD_MAGIC, 2)
LCD_WR_VALUE      = _IOW(LCD_MAGIC, 3, struct.calcsize("i"))
LCD_RD_VALUE      = _IOR(LCD_MAGIC, 4, ctypes.sizeof(LcdData))
LCD_SHIFT         = _IOW(LCD_MAGIC, 5, struct.calcsize("i"))
LCD_CURSOR_RETURN = _IO(LCD_MAGIC, 6)

# Open the device
fd = os.open("/dev/lcd1602_0", os.O_RDWR)

# Clear screen
fcntl.ioctl(fd, LCD_CLEAR_SCREEN)

# write again to screen
val = b'hello from Python'
fcntl.ioctl(fd, LCD_WR_VALUE, val)
time.sleep(1)

# screen OFF
fcntl.ioctl(fd, LCD_BACKLIGHT_OFF)
time.sleep(1)

# screen ON
fcntl.ioctl(fd, LCD_BACKLIGHT_ON)
time.sleep(1)

# write again to screen
val = b'hello from Python once more'
fcntl.ioctl(fd, LCD_WR_VALUE, val)
time.sleep(1)

# shift display right
val = ShiftData()
val.sc = 1
val.rl = 1
fcntl.ioctl(fd, LCD_SHIFT, val)
time.sleep(1)

# shift display
val = ShiftData()
val.sc = 1
val.rl = 1
fcntl.ioctl(fd, LCD_SHIFT, val)
time.sleep(1)

# read from displa
lcd_data = LcdData()
fcntl.ioctl(fd, LCD_RD_VALUE, lcd_data)
# Convert to string
result = lcd_data.data.decode('ascii')
print("result: ", result)

# shift cursor right
val.sc = 0
val.rl = 1
fcntl.ioctl(fd, LCD_SHIFT, val)
time.sleep(1)

# shift cursor right
val.sc = 0
val.rl = 1
fcntl.ioctl(fd, LCD_SHIFT, val)
time.sleep(1)

# Cursor return
fcntl.ioctl(fd, LCD_CURSOR_RETURN)
time.sleep(1)

os.close(fd)
