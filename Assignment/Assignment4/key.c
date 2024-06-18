#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h> 
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/delay.h>

static int keyboard_open(struct inode *, struct file *);
static int keyboard_close(struct inode *, struct file *);
static ssize_t keyboard_read(struct file *, char *, size_t, loff_t *);
static ssize_t keyboard_write(struct file *, const char *, size_t, loff_t *);

#define MAX 32
#define KBD_DATA_REG        0x60    /* I/O port for keyboard data */
#define KBD_CONTROL_REG     0x64
#define DELAY do { mdelay(1); if (++delay > 10) break; } while(0)

int sleep_duration = 10000;
module_param(sleep_duration, int, S_IRUGO);

static struct kfifo buf;
static int major;
static dev_t devno;
static struct class *pclass;
static struct cdev cdev;
static struct file_operations keyboard_fops = {
    .owner = THIS_MODULE,
    .open = keyboard_open,
    .release = keyboard_close,
    .read = keyboard_read,
    .write = keyboard_write
};

static void disable_keyboard(void)
{
    long delay = 0;
    // Wait till the input buffer is empty
    while (inb(KBD_CONTROL_REG) & 0x02)
        DELAY;
    outb(0xAD, KBD_CONTROL_REG);
    printk(KERN_INFO "Keyboard disabled.\n");
}

static void enable_keyboard(void)
{
    long delay = 0;
    // Wait till the input buffer is empty
    while (inb(KBD_CONTROL_REG) & 0x02)
        DELAY;
    outb(0xAE, KBD_CONTROL_REG);
    printk(KERN_INFO "Keyboard enabled.\n");
}

static __init int keyboard_init(void) {
    int ret, minor;
    long delay = 0;
    struct device *pdevice;
    
    printk(KERN_INFO "%s: keyboard_init() called.\n", THIS_MODULE->name);
    ret = kfifo_alloc(&buf, MAX, GFP_KERNEL);
    if(ret != 0) {
        printk(KERN_ERR "%s: kfifo_alloc() failed.\n", THIS_MODULE->name);
        goto kfifo_alloc_failed;
    }
    printk(KERN_INFO "%s: kfifo_alloc() successfully created device.\n", THIS_MODULE->name);

    ret = alloc_chrdev_region(&devno, 0, 1, "keyboard");
    if(ret != 0) {
        printk(KERN_ERR "%s: alloc_chrdev_region() failed.\n", THIS_MODULE->name);
        goto alloc_chrdev_region_failed;
    }
    major = MAJOR(devno);
    minor = MINOR(devno);
    printk(KERN_INFO "%s: alloc_chrdev_region() allocated device number %d/%d.\n", THIS_MODULE->name, major, minor);

    pclass = class_create(THIS_MODULE, "keyboard_class");
    if(IS_ERR(pclass)) {
        printk(KERN_ERR "%s: class_create() failed.\n", THIS_MODULE->name);
        ret = PTR_ERR(pclass);
        goto class_create_failed;
    }
    printk(KERN_INFO "%s: class_create() created device class.\n", THIS_MODULE->name);

    pdevice = device_create(pclass, NULL, devno, NULL, "keyboard%d", 0);
    if(IS_ERR(pdevice)) {
        printk(KERN_ERR "%s: device_create() failed.\n", THIS_MODULE->name);
        ret = PTR_ERR(pdevice);
        goto device_create_failed;
    }
    printk(KERN_INFO "%s: device_create() created device file.\n", THIS_MODULE->name);

    cdev_init(&cdev, &keyboard_fops);
    ret = cdev_add(&cdev, devno, 1);  
    if(ret != 0) {
        printk(KERN_ERR "%s: cdev_add() failed to add cdev in kernel db.\n", THIS_MODULE->name);
        goto cdev_add_failed;
    }
    printk(KERN_INFO "%s: cdev_add() added device in kernel db.\n", THIS_MODULE->name);

    disable_keyboard();
    msleep(sleep_duration);
    enable_keyboard();

    return 0;

cdev_add_failed:
    device_destroy(pclass, devno);
device_create_failed:
    class_destroy(pclass);
class_create_failed:
    unregister_chrdev_region(devno, 1);
alloc_chrdev_region_failed:
    kfifo_free(&buf);
kfifo_alloc_failed:
    return ret;
}

static __exit void keyboard_exit(void) {
    printk(KERN_INFO "%s: keyboard_exit() called.\n", THIS_MODULE->name);
    cdev_del(&cdev);
    printk(KERN_INFO "%s: cdev_del() removed device from kernel db.\n", THIS_MODULE->name);
    device_destroy(pclass, devno);
    printk(KERN_INFO "%s: device_destroy() destroyed device file.\n", THIS_MODULE->name);
    class_destroy(pclass);
    printk(KERN_INFO "%s: class_destroy() destroyed device class.\n", THIS_MODULE->name);
    unregister_chrdev_region(devno, 1);
    printk(KERN_INFO "%s: unregister_chrdev_region() released device number.\n", THIS_MODULE->name);
    kfifo_free(&buf);
    printk(KERN_INFO "%s: kfifo_free() destroyed device.\n", THIS_MODULE->name);
}

static int keyboard_open(struct inode *pinode, struct file *pfile) {
    printk(KERN_INFO "%s: keyboard_open() called.\n", THIS_MODULE->name);
    return 0;
}

static int keyboard_close(struct inode *pinode, struct file *pfile) {
    printk(KERN_INFO "%s: keyboard_close() called.\n", THIS_MODULE->name);
    return 0;
}

static ssize_t keyboard_read(struct file *pfile, char *ubuf, size_t size, loff_t *poffset) {
    int nbytes, ret;
    printk(KERN_INFO "%s: keyboard_read() called.\n", THIS_MODULE->name);
    ret = kfifo_to_user(&buf, ubuf, size, &nbytes);
    if(ret < 0) {
        printk(KERN_ERR "%s: keyboard_read() failed to copy data from kernel space using kfifo_to_user().\n", THIS_MODULE->name);
        return ret;     
    }
    printk(KERN_INFO "%s: keyboard_read() copied %d bytes to user space.\n", THIS_MODULE->name, nbytes);
    return nbytes;
}

static ssize_t keyboard_write(struct file *pfile, const char *ubuf, size_t size, loff_t *poffset) {
    int nbytes, ret;
    printk(KERN_INFO "%s: keyboard_write() called.\n", THIS_MODULE->name);
    ret = kfifo_from_user(&buf, ubuf, size, &nbytes);
    if(ret < 0) {
        printk(KERN_ERR "%s: keyboard_write() failed to copy data in kernel space using kfifo_from_user().\n", THIS_MODULE->name);
        return ret;     
    }
    printk(KERN_INFO "%s: keyboard_write() copied %d bytes from user space.\n", THIS_MODULE->name, nbytes);
    return nbytes;
}

module_init(keyboard_init);
module_exit(keyboard_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Omkar Udavant");
MODULE_DESCRIPTION("Simple keyboard driver device.");

