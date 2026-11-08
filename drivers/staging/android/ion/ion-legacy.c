// SPDX-License-Identifier: GPL-2.0
/*
 * Legacy ION client and ioctl ABI adapted from the working 4.4 driver.
 * Copyright (C) 2011, 2013 Google, Inc.
 * Copyright (c) 2016, 2018, The Linux Foundation. All rights reserved.
 */
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/file.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "ion.h"
#include "ion-legacy.h"

/* Keep the native and compat layouts of the original handle-based ABI. */
struct ion_legacy_allocation {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	int handle;
};

struct compat_ion_legacy_allocation {
	compat_size_t len;
	compat_size_t align;
	compat_uint_t heap_id_mask;
	compat_uint_t flags;
	compat_int_t handle;
};

struct ion_legacy_fd {
	int handle;
	int fd;
};

struct ion_legacy_custom {
	unsigned int cmd;
	unsigned long arg;
};

struct compat_ion_legacy_custom {
	compat_uint_t cmd;
	compat_ulong_t arg;
};

struct ion_legacy_flush {
	int handle;
	int fd;
	void __user *vaddr;
	unsigned int offset;
	unsigned int length;
};

struct compat_ion_legacy_flush {
	compat_int_t handle;
	compat_int_t fd;
	compat_uptr_t vaddr;
	compat_uint_t offset;
	compat_uint_t length;
};

struct ion_legacy_handle {
	struct dma_buf *buf;
	unsigned int users;
};

struct ion_legacy_client {
	struct mutex lock;
	struct idr handles;
};

int ion_legacy_open(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	mutex_init(&client->lock);
	idr_init(&client->handles);
	file->private_data = client;
	return 0;
}

int ion_legacy_release(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		dma_buf_put(handle->buf);
		kfree(handle);
	}
	idr_destroy(&client->handles);
	kfree(client);
	return 0;
}

/* Consume the dma-buf reference, including on failure. Caller holds lock. */
static int ion_legacy_add(struct ion_legacy_client *client, struct dma_buf *buf)
{
	struct ion_legacy_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		if (handle->buf != buf)
			continue;
		if (handle->users == UINT_MAX)
			id = -EOVERFLOW;
		else
			handle->users++;
		dma_buf_put(buf);
		return id;
	}

	handle = kmalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle) {
		dma_buf_put(buf);
		return -ENOMEM;
	}
	handle->buf = buf;
	handle->users = 1;
	id = idr_alloc(&client->handles, handle, 1, 0, GFP_KERNEL);
	if (id < 0) {
		dma_buf_put(buf);
		kfree(handle);
	}
	return id;
}

static int ion_legacy_free(struct ion_legacy_client *client, int id)
{
	struct ion_legacy_handle *handle = idr_find(&client->handles, id);

	if (!handle)
		return -EINVAL;
	if (!--handle->users) {
		idr_remove(&client->handles, id);
		dma_buf_put(handle->buf);
		kfree(handle);
	}
	return 0;
}

static struct dma_buf *ion_legacy_get(struct ion_legacy_client *client,
				     int id, int fd)
{
	struct ion_legacy_handle *handle;
	struct dma_buf *buf;

	if (id > 0) {
		handle = idr_find(&client->handles, id);
		if (!handle)
			return ERR_PTR(-EINVAL);
		buf = handle->buf;
		get_dma_buf(buf);
	} else {
		buf = dma_buf_get(fd);
		if (IS_ERR(buf))
			return buf;
		if (!is_ion_dma_buf(buf)) {
			dma_buf_put(buf);
			return ERR_PTR(-EINVAL);
		}
	}
	return buf;
}

static long ion_legacy_alloc(struct ion_legacy_client *client,
			      unsigned int cmd, void __user *arg, bool compat)
{
	struct ion_legacy_allocation data = { };
	struct compat_ion_legacy_allocation data32;
	struct dma_buf *buf;
	int id, ret;

	if (compat) {
		if (_IOC_SIZE(cmd) != sizeof(data32))
			return -ENOTTY;
		if (copy_from_user(&data32, arg, sizeof(data32)))
			return -EFAULT;
		data.len = data32.len;
		data.align = data32.align;
		data.heap_id_mask = data32.heap_id_mask;
		data.flags = data32.flags;
	} else {
		if (_IOC_SIZE(cmd) != sizeof(data))
			return -ENOTTY;
		if (copy_from_user(&data, arg, sizeof(data)))
			return -EFAULT;
	}

	/* The 4.19 allocator guarantees page alignment. */
	if (data.align > PAGE_SIZE ||
	    (data.align && !is_power_of_2(data.align)))
		return -EINVAL;

	buf = ion_alloc_dmabuf(data.len, data.heap_id_mask, data.flags);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	id = ion_legacy_add(client, buf);
	if (id < 0)
		return id;

	if (compat) {
		data32.handle = id;
		ret = copy_to_user(arg, &data32, sizeof(data32));
	} else {
		data.handle = id;
		ret = copy_to_user(arg, &data, sizeof(data));
	}
	if (ret) {
		ion_legacy_free(client, id);
		return -EFAULT;
	}
	return 0;
}

static long ion_legacy_cache(struct ion_legacy_client *client,
			      unsigned int cmd, void __user *arg, bool compat)
{
	struct ion_legacy_flush data;
	struct compat_ion_legacy_flush data32;
	struct dma_buf *buf;
	int ret;

	if (_IOC_TYPE(cmd) != 'M' || _IOC_NR(cmd) > 2 ||
	    _IOC_DIR(cmd) != (_IOC_READ | _IOC_WRITE))
		return -ENOTTY;
	if (compat) {
		if (_IOC_SIZE(cmd) != sizeof(data32))
			return -ENOTTY;
		if (copy_from_user(&data32, arg, sizeof(data32)))
			return -EFAULT;
		data.handle = data32.handle;
		data.fd = data32.fd;
		data.offset = data32.offset;
		data.length = data32.length;
	} else {
		if (_IOC_SIZE(cmd) != sizeof(data))
			return -ENOTTY;
		if (copy_from_user(&data, arg, sizeof(data)))
			return -EFAULT;
	}

	buf = ion_legacy_get(client, data.handle, data.fd);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	if (data.offset > buf->size || data.length > buf->size - data.offset) {
		ret = -EINVAL;
		goto out;
	}

	if (_IOC_NR(cmd) == 0)
		ret = dma_buf_end_cpu_access_partial(buf, DMA_TO_DEVICE,
						 data.offset, data.length);
	else if (_IOC_NR(cmd) == 1)
		ret = dma_buf_begin_cpu_access_partial(buf, DMA_FROM_DEVICE,
						   data.offset, data.length);
	else {
		ret = dma_buf_begin_cpu_access_partial(buf, DMA_BIDIRECTIONAL,
						   data.offset, data.length);
		if (!ret)
			ret = dma_buf_end_cpu_access_partial(buf, DMA_BIDIRECTIONAL,
						     data.offset, data.length);
	}
out:
	dma_buf_put(buf);
	return ret;
}

long ion_legacy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_fd data;
	struct ion_legacy_custom custom;
	struct compat_ion_legacy_custom custom32;
	struct dma_buf *buf;
	void __user *uptr = (void __user *)arg;
	bool compat = in_compat_syscall();
	int ret = -ENOTTY, fd, id;

	mutex_lock(&client->lock);
	if (_IOC_TYPE(cmd) == 'M') {
		ret = ion_legacy_cache(client, cmd, uptr, compat);
		goto out;
	}
	if (_IOC_TYPE(cmd) != ION_IOC_MAGIC ||
	    _IOC_DIR(cmd) != (_IOC_READ | _IOC_WRITE))
		goto out;

	switch (_IOC_NR(cmd)) {
	case 0:
		ret = ion_legacy_alloc(client, cmd, uptr, compat);
		break;
	case 1:
		if (_IOC_SIZE(cmd) != sizeof(id))
			break;
		if (copy_from_user(&id, uptr, sizeof(id))) {
			ret = -EFAULT;
			break;
		}
		ret = ion_legacy_free(client, id);
		break;
	case 2: /* map */
	case 4: /* share */
	case 5: /* import */
	case 7: /* sync */
		if (_IOC_SIZE(cmd) != sizeof(data))
			break;
		if (copy_from_user(&data, uptr, sizeof(data))) {
			ret = -EFAULT;
			break;
		}
		if ((_IOC_NR(cmd) == 2 || _IOC_NR(cmd) == 4) && data.handle <= 0) {
			ret = -EINVAL;
			break;
		}
		buf = ion_legacy_get(client,
			(_IOC_NR(cmd) == 2 || _IOC_NR(cmd) == 4) ? data.handle : 0,
			data.fd);
		if (IS_ERR(buf)) {
			ret = PTR_ERR(buf);
			break;
		}
		if (_IOC_NR(cmd) == 5) {
			id = ion_legacy_add(client, buf);
			ret = id < 0 ? id : 0;
			if (ret)
				break;
			data.handle = id;
			if (copy_to_user(uptr, &data, sizeof(data))) {
				ion_legacy_free(client, id);
				ret = -EFAULT;
			}
		} else if (_IOC_NR(cmd) == 7) {
			ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
			if (!ret)
				ret = dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);
			dma_buf_put(buf);
		} else {
			fd = get_unused_fd_flags(O_CLOEXEC);
			ret = fd;
			if (fd < 0) {
				dma_buf_put(buf);
				break;
			}
			data.fd = fd;
			if (copy_to_user(uptr, &data, sizeof(data))) {
				put_unused_fd(fd);
				dma_buf_put(buf);
				ret = -EFAULT;
				break;
			}
			fd_install(fd, buf->file);
			ret = 0;
		}
		break;
	case 6:
		if (compat) {
			if (_IOC_SIZE(cmd) != sizeof(custom32))
				break;
			if (copy_from_user(&custom32, uptr, sizeof(custom32))) {
				ret = -EFAULT;
				break;
			}
			custom.cmd = custom32.cmd;
			custom.arg = (unsigned long)compat_ptr(custom32.arg);
		} else {
			if (_IOC_SIZE(cmd) != sizeof(custom))
				break;
			if (copy_from_user(&custom, uptr, sizeof(custom))) {
				ret = -EFAULT;
				break;
			}
		}
		ret = ion_legacy_cache(client, custom.cmd,
				       (void __user *)custom.arg, compat);
		break;
	}
out:
	mutex_unlock(&client->lock);
	return ret;
}
