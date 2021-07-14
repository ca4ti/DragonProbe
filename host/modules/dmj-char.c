// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: character device for userspace
 * access while the module is loaded
 *
 * Copyright (c) sys64738 and haskal
 */

#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/device.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0) /* TODO: make this check more precise */
#include <linux/device/class.h>
#endif
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#if 0
#include <linux/mfd/dmj.h>
#else
#include "dmj.h"
#endif

#define HARDWARE_NAME "DapperMime-JTAG"
#define DEVICE_NAME "dmj-char"
#define CLASS_NAME "dmj"

struct dmj_char_dev {
	struct cdev cdev;
	struct device *dev;
	struct platform_device *pdev;
	int minor;
};

static int n_cdevs = 0;
static spinlock_t ndevs_lock;

static int dmj_char_major;
static struct class *dmj_char_class;

static ssize_t dmj_char_read(struct file *file, char *buf, size_t len, loff_t *loff)
{
	int res, ilen;
	unsigned long ret;
	struct dmj_char_dev *dmjch = file->private_data;
	void *kbuf = NULL;
	struct device *dev = dmjch->dev;

	if (len > INT_MAX) return -EINVAL;
	ilen = (int)len;

	res = dmj_transfer(dmjch->pdev, -1, DMJ_XFER_FLAGS_FILL_RECVBUF, NULL, 0, &kbuf, &ilen);
	if (res < 0 || ilen < 0 || !kbuf) {
		if (kbuf) kfree(kbuf);
		return (res >= 0) ? -EIO : ret;
	}

	ret = copy_to_user(buf, kbuf, (size_t)ilen);
	kfree(kbuf);

	if (ret) return -EFAULT;

	return (ssize_t)ilen;
}
static ssize_t dmj_char_write(struct file *file, const char *buf, size_t len, loff_t *off)
{
	unsigned long ret;
	int res;
	struct dmj_char_dev *dmjch = file->private_data;
	void *kbuf;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf) return -ENOMEM;

	ret = copy_from_user(kbuf, buf, len);
	if (ret) {
		kfree(kbuf);
		return -EFAULT;
	}

	res = dmj_transfer(dmjch->pdev, -1, 0, kbuf, len, NULL, NULL);

	kfree(kbuf);
	return (res < 0) ? res : len;
}

static int dmj_char_open(struct inode *inode, struct file *file)
{
	struct dmj_char_dev *dmjch;

	dmjch = container_of(inode->i_cdev, struct dmj_char_dev, cdev);

	file->private_data = dmjch;

	return 0;
}
static int dmj_char_release(struct inode *inode, struct file *file)
{
	struct dmj_char_dev *dmjch;

	dmjch = container_of(inode->i_cdev, struct dmj_char_dev, cdev);

	return 0;
}

static const struct file_operations dmj_char_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = dmj_char_read,
	.write = dmj_char_write,
	.unlocked_ioctl = NULL,
	.open = dmj_char_open,
	.release = dmj_char_release
};

static int dmj_char_probe(struct platform_device *pdev)
{
	int ret, minor;
	struct device *device;
	struct device *pd = &pdev->dev;
	struct dmj_char_dev *dmjch;

	spin_lock(&ndevs_lock);
	minor = n_cdevs;
	++n_cdevs;
	spin_unlock(&ndevs_lock);

	dev_info(pd, HARDWARE_NAME " /dev entries driver, major=%d, minor=%d\n",
			dmj_char_major, minor);

	dmjch = devm_kzalloc(pd, sizeof(*dmjch), GFP_KERNEL);
	if (!dmjch) return -ENOMEM;

	platform_set_drvdata(pdev, dmjch);

	cdev_init(&dmjch->cdev, &dmj_char_fops);
	ret = cdev_add(&dmjch->cdev, MKDEV(dmj_char_major, minor), 1);
	if (ret < 0) {
		dev_err(pd, "failed to create cdev: %d\n", ret);
		return ret;
	}

	device = device_create(dmj_char_class, pd, MKDEV(dmj_char_major, minor), dmjch, "dmj-%d", minor);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		dev_err(pd, "failed to create device: %d\n", ret);
		cdev_del(&dmjch->cdev);
		return ret;
	}

	dev_notice(device, "created device /dev/dmj-%d\n", minor);

	dmjch->dev = device;
	dmjch->minor = minor;
	dmjch->pdev = pdev;

	return 0;
}
static int dmj_char_remove(struct platform_device *pdev)
{
	struct dmj_char_dev *dmjch = platform_get_drvdata(pdev);

	device_destroy(dmj_char_class, MKDEV(dmj_char_major, dmjch->minor));
	cdev_del(&dmjch->cdev);
	unregister_chrdev(MKDEV(dmj_char_major, dmjch->minor), CLASS_NAME);

	return 0;
}

static struct platform_driver dmj_char_driver = {
	.driver = {
		.name = "dmj-char"
	},
	.probe  = dmj_char_probe,
	.remove = dmj_char_remove
};
/*module_platform_driver(dmj_char_driver);*/

static int __init dmj_char_init(void)
{
	int ret, major;
	struct class *class;
	dev_t devid;

	spin_lock_init(&ndevs_lock);

	n_cdevs = 0;

	devid = MKDEV(0,0);
	ret = alloc_chrdev_region(&devid, 0, 256, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR " failed to register chrdev major number: %d\n", major);
		return ret;
	}
	major = MAJOR(devid);
	printk(KERN_NOTICE DEVICE_NAME " registered with major number %d\n", major);

	class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(class)) {
		ret = PTR_ERR(class);
		printk(KERN_ERR " failed to create class: %d\n", ret);
		unregister_chrdev(major, DEVICE_NAME);
		return ret;
	}
	printk(KERN_DEBUG DEVICE_NAME " created class\n");

	dmj_char_major = major;
	dmj_char_class = class;

	platform_driver_register(&dmj_char_driver);

	return 0;
}
static void __exit dmj_char_exit(void)
{
	platform_driver_unregister(&dmj_char_driver);

	spin_lock(&ndevs_lock);
	n_cdevs = 0;
	spin_unlock(&ndevs_lock);

	class_destroy(dmj_char_class);
	unregister_chrdev(dmj_char_major, CLASS_NAME);

	dmj_char_major = -1;
	dmj_char_class = NULL;
}

module_init(dmj_char_init);
module_exit(dmj_char_exit);

MODULE_AUTHOR("sys64738 <sys64738@disroot.org>");
MODULE_AUTHOR("haskal <haskal@awoo.systems>");
MODULE_DESCRIPTION("Character device for the " HARDWARE_NAME " USB multitool");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dmj-char");

