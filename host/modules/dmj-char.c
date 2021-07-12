// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the DapperMime-JTAG USB multitool: character device for userspace
 * access while the module is loaded
 *
 * Copyright (c) sys64738 and haskal
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/device/class.h>
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

#define DMJ_READ_BUFSIZE 64

struct dmj_char_dev {
	struct cdev cdev;
	struct device *dev;
	struct platform_device *pdev;
	int minor;
	spinlock_t devopen_lock;

	size_t bufpos;
	uint8_t rdbuf[DMJ_READ_BUFSIZE];
};

static int n_cdevs = 0;
static spinlock_t ndevs_lock;

static int dmj_char_major;
static struct class *dmj_char_class;

static ssize_t dmj_char_read(struct file *file, char *buf, size_t len, loff_t *loff)
{
	int res, bsize;
	struct dmj_char_dev *dmjch;
	size_t bpos, todo, done = 0;
	uint8_t *kbuf;
	struct device *dev;

	kbuf = kmalloc(len + DMJ_READ_BUFSIZE, GFP_KERNEL);
	if (!kbuf) return -ENOMEM;

	dmjch = file->private_data;
	dev = dmjch->dev;
	todo = len;

	spin_lock(&dmjch->devopen_lock);
	bpos = dmjch->bufpos;

	/* TODO: ioctl to control this buffering? */
	if (bpos) { /* data in buffer */
		if (len <= DMJ_READ_BUFSIZE - bpos) {
			/* doable in a single copy, no USB xfers needed */
			memcpy(kbuf, &dmjch->rdbuf[bpos], len);
			bpos += len;
			if (bpos == DMJ_READ_BUFSIZE) bpos = 0;

			done += len;
			todo -= len;
		} else {
			/* initial copy stuff */
			memcpy(kbuf, &dmjch->rdbuf[bpos], DMJ_READ_BUFSIZE - bpos);
			todo -= DMJ_READ_BUFSIZE - bpos;
			done += DMJ_READ_BUFSIZE - bpos;
			bpos = 0;
		}
	}

	if /*while*/ (todo) { /* TODO: do we want a while here? */
		bsize = DMJ_READ_BUFSIZE;

		res = dmj_read(dmjch->pdev, 0, &kbuf[done], &bsize);
		if (res < 0 || bsize < 0) {
			/* ah snap */
			spin_unlock(&dmjch->devopen_lock);
			return res;
		}

		if ((size_t)bsize > todo) {
			if ((size_t)bsize > todo + DMJ_READ_BUFSIZE) {
				/* can't hold all this data, time to bail out... */
				dev_err(dev, "too much data (%zu B excess), can't buffer, AAAAAA",
						(size_t)bsize - (todo + DMJ_READ_BUFSIZE));
				spin_unlock(&dmjch->devopen_lock);
				BUG(); /* some stuff somewhere will have been corrupted, so, get out while we can */
			}

			/* stuff for next call */
			done = todo;
			bpos = DMJ_READ_BUFSIZE - ((size_t)bsize - todo);
			memcpy(&dmjch->rdbuf[bpos], &kbuf[done], (size_t)bsize - todo);
			todo = 0;
		} else {
			todo -= (size_t)bsize;
			done += (size_t)bsize;
		}
	}

	dmjch->bufpos = bpos;
	spin_unlock(&dmjch->devopen_lock);

	res = copy_to_user(buf, kbuf, len);
	if (res) return (res < 0) ? res : -EFAULT;

	return done;
}
static ssize_t dmj_char_write(struct file *file, const char *buf, size_t len, loff_t *off)
{
	unsigned long ret;
	struct dmj_char_dev *dmjch;
	void *kbuf;

	dmjch = file->private_data;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf) return -ENOMEM;

	ret = copy_from_user(kbuf, buf, len);
	if (ret) {
		kfree(kbuf);
		return (ret < 0) ? ret : -EFAULT;
	}

	ret = dmj_transfer(dmjch->pdev, -1, 0, kbuf, len, NULL, NULL);
	if (ret < 0) {
		kfree(kbuf);
		return ret;
	}

	kfree(kbuf);
	return len;
}

static int dmj_char_open(struct inode *inode, struct file *file)
{
	struct dmj_char_dev *dmjch;
	int ret;

	dmjch = container_of(inode->i_cdev, struct dmj_char_dev, cdev);

	spin_lock(&dmjch->devopen_lock);
	ret = dmjch->bufpos;
	if (~ret == 0) dmjch->bufpos = 0;
	spin_unlock(&dmjch->devopen_lock);

	if (~ret != 0) return -ETXTBSY; // already open

	file->private_data = dmjch;

	return 0;
}
static int dmj_char_release(struct inode *inode, struct file *file)
{
	struct dmj_char_dev *dmjch;

	dmjch = container_of(inode->i_cdev, struct dmj_char_dev, cdev);

	spin_lock(&dmjch->devopen_lock);
	dmjch->bufpos = ~(size_t)0;
	spin_unlock(&dmjch->devopen_lock);

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
	dmjch->bufpos = ~(size_t)0;

	spin_lock_init(&dmjch->devopen_lock);

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
		unregister_chrdev(major, DEVICE_NAME); /* TODO: unregister_chrdev_rage */
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
	unregister_chrdev(MKDEV(dmj_char_major, 0), CLASS_NAME); /* TODO: unregister_chrdev_rage */

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

