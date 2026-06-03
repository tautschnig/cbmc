// Test module that proves the KASAN detection mechanism for the
// buffer_from_user pattern: a fixed-size kmalloc buffer with a
// copy_from_user that overshoots.  Uses a SMALL buffer (192 bytes)
// so it stays in slab (with a redzone), unlike vme_user's 128KiB
// kmalloc which fills its compound page exactly.
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define BUF_SIZE 192

static char *kern_buf;

static ssize_t poc_write(struct file *f, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	// Intentionally do NOT clamp count to BUF_SIZE,
	// mimicking buffer_from_user's missing clamp.
	if (copy_from_user(kern_buf, buf, count))
		return -EFAULT;
	return count;
}

static const struct file_operations poc_fops = {
	.owner = THIS_MODULE,
	.write = poc_write,
};

static struct miscdevice poc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "kasan_poc",
	.fops = &poc_fops,
};

static int __init poc_init(void)
{
	kern_buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (!kern_buf)
		return -ENOMEM;
	return misc_register(&poc_dev);
}

static void __exit poc_exit(void)
{
	misc_deregister(&poc_dev);
	kfree(kern_buf);
}

module_init(poc_init);
module_exit(poc_exit);
MODULE_LICENSE("GPL");
