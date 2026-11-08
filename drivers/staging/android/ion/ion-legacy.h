/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ION_LEGACY_H
#define _ION_LEGACY_H

#include <linux/fs.h>

#ifdef CONFIG_ION_LEGACY
int ion_legacy_open(struct inode *inode, struct file *file);
int ion_legacy_release(struct inode *inode, struct file *file);
long ion_legacy_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
static inline int ion_legacy_open(struct inode *inode, struct file *file)
{
	return 0;
}
static inline int ion_legacy_release(struct inode *inode, struct file *file)
{
	return 0;
}
static inline long ion_legacy_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	return -ENOTTY;
}
#endif
#endif
