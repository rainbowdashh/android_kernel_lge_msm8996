/* Copyright (c) 2017, Elliott Mitchell. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * version 3, or any later versions as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/init.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>
#include <soc/qcom/smem.h>

#include "dirtysanta_fixup.h"


#define LG_MODEL_NAME_SIZE 22
#define LG_NT_CODE_SIZE 2048
#define DIRTYSANTA_FIELD_LOG_SIZE 64

#ifdef CONFIG_DIRTYSANTA_FIXUP_DEBUG
#define DIRTYSANTA_SCAN_WINDOW 96
#define DIRTYSANTA_WIDE_SCAN_MAX (256 * 1024)
#endif


struct lge_smem_vendor0 {
	/* following 2 fields are common to all devices */
	int hw_rev;
	char model_name[10]; /* "MSM8996_H1" */
#ifdef CONFIG_DIRTYSANTA_FIXUP_LUCYE_VENDOR0_LAYOUT
	/* Keep these offsets aligned with LG G6/Lucye aboot's VENDOR0 view. */
	char reserved0[64];
	char nt_code[LG_NT_CODE_SIZE];
	char lg_model_name[LG_MODEL_NAME_SIZE];
	int sim_num;
	int svn_val;
	int flag_gpio;
#else
	/* following fields depend on the device */
	char nt_code[LG_NT_CODE_SIZE];
	char lg_model_name[LG_MODEL_NAME_SIZE];
	int sim_num;
	int flag_gpio;

	/* the structure in memory is 2096 bytes, likely for alignment */
#endif
};


static char ds_dev_name[LG_MODEL_NAME_SIZE] __initdata=CONFIG_DIRTYSANTA_FIXUP_DEVICENAME;
static char ds_nt_code[LG_NT_CODE_SIZE] __initdata=CONFIG_DIRTYSANTA_FIXUP_NTCODE;
static char sim_num __initdata=CONFIG_DIRTYSANTA_FIXUP_SIMCOUNT;


#ifdef CONFIG_DIRTYSANTA_FIXUP_DEBUG
struct dirtysanta_smem_item {
	unsigned id;
	const char *name;
};


struct dirtysanta_smem_view {
	unsigned to_proc;
	unsigned flags;
	const char *name;
};


static const char * const dirtysanta_tokens[] = {
	"E1506D36",
	"106D36",
	"LGM-G600L",
	"LG-H870",
	"G600",
	"H870",
};


static const char * const dirtysanta_bad_tokens[] = {
	"E1506D36",
	"106D36",
	"LGM-G600L",
	"G600",
};


static const struct dirtysanta_smem_item dirtysanta_scan_items[] = {
	{ SMEM_ID_VENDOR1, "VENDOR1" },
	{ SMEM_ID_VENDOR2, "VENDOR2" },
	{ SMEM_HW_SW_BUILD_ID, "HW_SW_BUILD_ID" },
	{ SMEM_MODEM_SW_BUILD_ID, "MODEM_SW_BUILD_ID" },
	{ SMEM_BOOT_INFO_FOR_APPS, "BOOT_INFO_FOR_APPS" },
	{ SMEM_IMAGE_VERSION_TABLE, "IMAGE_VERSION_TABLE" },
};


static const struct dirtysanta_smem_view dirtysanta_scan_views[] = {
	{ 0, SMEM_ANY_HOST_FLAG, "common" },
	{ SMEM_MODEM, 0, "modem" },
	{ SMEM_Q6, 0, "q6" },
	{ SMEM_MODEM_Q6_FW, 0, "modem_q6_fw" },
};
#endif


/*
 * SMEM fields may be only byte-aligned on some layouts.  Keep writes bytewise
 * so compiler-generated memcpy()/strlcpy() does not fault on unaligned fields.
 */
static void dirtysanta_write_smem_string(char *dst, const char *src,
unsigned size)
{
	volatile char *out=(volatile char *)dst;
	unsigned i;

	if(!size)
		return;

	for(i=0; i<size-1&&src[i]; ++i)
		out[i]=src[i];

	for(; i<size; ++i)
		out[i]='\0';
}


static void dirtysanta_log_field(const char *stage, const char *area,
const char *name, const char *data, unsigned len)
{
	char buf[DIRTYSANTA_FIELD_LOG_SIZE+1];
	unsigned i, use=min_t(unsigned, len, DIRTYSANTA_FIELD_LOG_SIZE);

	for(i=0; i<use; ++i)
		buf[i]=isprint((unsigned char)data[i]) ? data[i] : '.';
	buf[use]='\0';

	pr_info("DirtySanta: [%s] %s %s=\"%s\" raw=%*phN\n",
		stage, area, name, buf, use, data);
}


static void dirtysanta_log_vendor0_summary(const char *stage,
const char *area, struct lge_smem_vendor0 *ptr, unsigned size)
{
	const char *base=(const char *)ptr;

	pr_info("DirtySanta: [%s] %s size=%u struct=%zu offsets: hw_rev=%zu model_name=%zu nt_code=%zu lg_model_name=%zu sim_num=%zu flag_gpio=%zu\n",
		stage, area, size, sizeof(struct lge_smem_vendor0),
		offsetof(struct lge_smem_vendor0, hw_rev),
		offsetof(struct lge_smem_vendor0, model_name),
		offsetof(struct lge_smem_vendor0, nt_code),
		offsetof(struct lge_smem_vendor0, lg_model_name),
		offsetof(struct lge_smem_vendor0, sim_num),
		offsetof(struct lge_smem_vendor0, flag_gpio));

	if(size>=offsetof(struct lge_smem_vendor0, model_name)+sizeof(ptr->model_name)) {
		pr_info("DirtySanta: [%s] %s hw_rev=%d\n",
			stage, area, get_unaligned((const int *)(base+
				offsetof(struct lge_smem_vendor0, hw_rev))));
		dirtysanta_log_field(stage, area, "model_name",
			base+offsetof(struct lge_smem_vendor0, model_name),
			sizeof(ptr->model_name));
	}

	if(size>=offsetof(struct lge_smem_vendor0, nt_code)+sizeof(ptr->nt_code))
		dirtysanta_log_field(stage, area, "nt_code",
			base+offsetof(struct lge_smem_vendor0, nt_code),
			sizeof(ptr->nt_code));

	if(size>=offsetof(struct lge_smem_vendor0, lg_model_name)+sizeof(ptr->lg_model_name))
		dirtysanta_log_field(stage, area, "lg_model_name",
			base+offsetof(struct lge_smem_vendor0, lg_model_name),
			sizeof(ptr->lg_model_name));

	if(size>=offsetof(struct lge_smem_vendor0, sim_num)+sizeof(ptr->sim_num))
		pr_info("DirtySanta: [%s] %s sim_num=%d\n",
			stage, area, get_unaligned((const int *)(base+
				offsetof(struct lge_smem_vendor0, sim_num))));

	if(size>=offsetof(struct lge_smem_vendor0, flag_gpio)+sizeof(ptr->flag_gpio))
		pr_info("DirtySanta: [%s] %s flag_gpio=%d\n",
			stage, area, get_unaligned((const int *)(base+
				offsetof(struct lge_smem_vendor0, flag_gpio))));
}


#ifdef CONFIG_DIRTYSANTA_FIXUP_DEBUG
static void dirtysanta_dump_window(const char *stage, const char *area,
const void *base, unsigned size, unsigned start, unsigned len)
{
	char prefix[128];

	if(start>=size)
		return;

	len=min(len, size-start);
	snprintf(prefix, sizeof(prefix), "DirtySanta: [%s] %s+0x%x: ",
		stage, area, start);
	print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_OFFSET, 16, 1,
		(const char *)base+start, len, true);
}


static bool dirtysanta_scan_token(const char *stage, const char *area,
const char *buf, unsigned size, const char *token, bool log_miss)
{
	unsigned len=strlen(token);
	unsigned i;
	bool found=false;

	if(!len||size<len)
		return false;

	for(i=0; i<=size-len; ++i) {
		unsigned j;
		unsigned start;

		for(j=0; j<len; ++j) {
			if(buf[i+j]!=token[j])
				break;
		}

		if(j!=len)
			continue;

		pr_info("DirtySanta: [%s] found \"%s\" in %s at offset %u\n",
			stage, token, area, i);
		start=i>32 ? i-32 : 0;
		dirtysanta_dump_window(stage, area, buf, size, start,
			DIRTYSANTA_SCAN_WINDOW);
		found=true;
	}

	if(!found&&log_miss)
		pr_info("DirtySanta: [%s] \"%s\" not found in %s\n",
			stage, token, area);

	return found;
}


static void dirtysanta_log_vendor0_contents(const char *stage,
const char *area, struct lge_smem_vendor0 *ptr, unsigned size)
{
	unsigned i;

	dirtysanta_log_vendor0_summary(stage, area, ptr, size);

	for(i=0; i<ARRAY_SIZE(dirtysanta_tokens); ++i)
		dirtysanta_scan_token(stage, area, (char *)ptr, size,
			dirtysanta_tokens[i], true);

	dirtysanta_dump_window(stage, area, ptr, size, 0, 64);
	dirtysanta_dump_window(stage, area, ptr, size,
		offsetof(struct lge_smem_vendor0, lg_model_name)-32, 128);
}


static void dirtysanta_log_smem_wide_scan(const char *stage)
{
	unsigned i, j, k;

	for(j=0; j<ARRAY_SIZE(dirtysanta_scan_views); ++j) {
		const struct dirtysanta_smem_view *view;
		unsigned available=0;
		unsigned unavailable=0;
		unsigned truncated=0;
		unsigned hits=0;
		unsigned long long bytes=0;

		view=dirtysanta_scan_views+j;

		for(i=0; i<SMEM_NUM_ITEMS; ++i) {
			char area[64];
			void *ptr;
			unsigned size=0;
			unsigned scan_size;
			bool found=false;

			ptr=smem_get_entry_no_rlock(i, &size, view->to_proc,
				view->flags);

			if(IS_ERR_OR_NULL(ptr)) {
				unavailable++;
				continue;
			}

			available++;
			bytes+=size;
			scan_size=min_t(unsigned, size, DIRTYSANTA_WIDE_SCAN_MAX);

			if(scan_size<size)
				truncated++;

			snprintf(area, sizeof(area), "wide %s SMEM[%u]",
				view->name, i);

			for(k=0; k<ARRAY_SIZE(dirtysanta_bad_tokens); ++k)
				found|=dirtysanta_scan_token(stage, area, ptr,
					scan_size, dirtysanta_bad_tokens[k],
					false);

			if(found) {
				hits++;
				if(scan_size<size)
					pr_info("DirtySanta: [%s] wide %s SMEM[%u] truncated scan size=%u scanned=%u\n",
						stage, view->name, i, size,
						scan_size);
			}
		}

		pr_info("DirtySanta: [%s] wide SMEM scan %s available=%u unavailable=%u bytes=%llu truncated=%u hit_items=%u\n",
			stage, view->name, available, unavailable, bytes,
			truncated, hits);
	}
}


static void dirtysanta_log_smem_global_scan(const char *stage)
{
	unsigned i, k;
	unsigned available=0;
	unsigned unavailable=0;
	unsigned truncated=0;
	unsigned hits=0;
	unsigned long long bytes=0;

	for(i=0; i<SMEM_NUM_ITEMS; ++i) {
		char area[64];
		void *ptr;
		unsigned size=0;
		unsigned scan_size;
		bool found=false;

		ptr=smem_get_entry_global_no_rlock(i, &size);

		if(IS_ERR_OR_NULL(ptr)) {
			unavailable++;
			continue;
		}

		available++;
		bytes+=size;
		scan_size=min_t(unsigned, size, DIRTYSANTA_WIDE_SCAN_MAX);

		if(scan_size<size)
			truncated++;

		snprintf(area, sizeof(area), "legacy global SMEM[%u]", i);

		for(k=0; k<ARRAY_SIZE(dirtysanta_bad_tokens); ++k)
			found|=dirtysanta_scan_token(stage, area, ptr,
				scan_size, dirtysanta_bad_tokens[k], false);

		if(found) {
			hits++;
			if(scan_size<size)
				pr_info("DirtySanta: [%s] legacy global SMEM[%u] truncated scan size=%u scanned=%u\n",
					stage, i, size, scan_size);
		}
	}

	pr_info("DirtySanta: [%s] legacy global SMEM scan available=%u unavailable=%u bytes=%llu truncated=%u hit_items=%u\n",
		stage, available, unavailable, bytes, truncated, hits);
}


static void dirtysanta_log_smem_scan(const char *stage)
{
	unsigned i, j, k;

	for(i=0; i<ARRAY_SIZE(dirtysanta_scan_items); ++i) {
		const struct dirtysanta_smem_item *item;

		item=dirtysanta_scan_items+i;

		for(j=0; j<ARRAY_SIZE(dirtysanta_scan_views); ++j) {
			const struct dirtysanta_smem_view *view;
			char area[64];
			void *ptr;
			unsigned size=0;
			bool found=false;

			view=dirtysanta_scan_views+j;
			ptr=smem_get_entry_no_rlock(item->id, &size,
				view->to_proc, view->flags);
			snprintf(area, sizeof(area), "%s %s",
				view->name, item->name);

			if(IS_ERR_OR_NULL(ptr)) {
				if(IS_ERR(ptr))
					pr_info("DirtySanta: [%s] scan %s unavailable err=%ld\n",
						stage, area, PTR_ERR(ptr));
				else
					pr_info("DirtySanta: [%s] scan %s unavailable\n",
						stage, area);
				continue;
			}

			for(k=0; k<ARRAY_SIZE(dirtysanta_tokens); ++k)
				found|=dirtysanta_scan_token(stage, area, ptr,
					size, dirtysanta_tokens[k], false);

			if(!found)
				pr_info("DirtySanta: [%s] scan %s size=%u no target tokens\n",
					stage, area, size);
		}
	}

	dirtysanta_log_smem_wide_scan(stage);
	dirtysanta_log_smem_global_scan(stage);
}


void dirtysanta_log_vendor0(const char *stage)
{
	struct lge_smem_vendor0 *common;
	struct lge_smem_vendor0 *modem;
	struct lge_smem_vendor0 *global;
	unsigned common_size=0;
	unsigned modem_size;
	unsigned global_size;
	bool common_valid=false;

	common=smem_get_entry(SMEM_ID_VENDOR0, &common_size, 0,
		SMEM_ANY_HOST_FLAG);
	if(IS_ERR_OR_NULL(common)) {
		pr_info("DirtySanta: [%s] common VENDOR0 unavailable\n",
			stage);
	} else {
		common_valid=true;
		dirtysanta_log_vendor0_contents(stage, "common VENDOR0",
			common, common_size);
	}

	global=smem_get_entry_global_no_rlock(SMEM_ID_VENDOR0, &global_size);
	if(IS_ERR_OR_NULL(global)) {
		pr_info("DirtySanta: [%s] legacy global VENDOR0 unavailable\n",
			stage);
	} else if(common_valid&&global==common&&global_size==common_size) {
		pr_info("DirtySanta: [%s] legacy global VENDOR0 resolves to common VENDOR0\n",
			stage);
	} else {
		dirtysanta_log_vendor0_contents(stage, "legacy global VENDOR0",
			global, global_size);
	}

	modem=smem_get_entry(SMEM_ID_VENDOR0, &modem_size, SMEM_MODEM, 0);
	if(IS_ERR_OR_NULL(modem)) {
		pr_info("DirtySanta: [%s] modem VENDOR0 unavailable\n", stage);
		dirtysanta_log_smem_scan(stage);
		return;
	}

	if(common_valid&&modem==common&&modem_size==common_size) {
		pr_info("DirtySanta: [%s] modem VENDOR0 resolves to common VENDOR0\n",
			stage);
		dirtysanta_log_smem_scan(stage);
		return;
	}

	dirtysanta_log_vendor0_contents(stage, "modem VENDOR0",
		modem, modem_size);

	dirtysanta_log_smem_scan(stage);
}
EXPORT_SYMBOL(dirtysanta_log_vendor0);
#endif


#ifdef CONFIG_DIRTYSANTA_FIXUP_SYSFS
typedef struct device_attribute attr_type;
static ssize_t dirtysanta_show(struct device *, attr_type *, char *);
static ssize_t dirtysanta_store(struct device *, attr_type *, const char *,
size_t);

static attr_type attrs[] = {
	__ATTR(dirtysanta_lg_model_name,
		S_IRUGO, dirtysanta_show, dirtysanta_store),
	__ATTR(dirtysanta_sim_num,
		S_IRUGO, dirtysanta_show, dirtysanta_store),
};
#endif


static int __init dirtysanta_fixup_loadcfg(void)
{
	const char MODELNAMEEQ[]="model.name=";
	const char SIMNUMEQ[]="lge.sim_num=";
	int dev_name_len;
	char *match;
	int ret=0;

	if(!ds_dev_name[0]) {
		if(!(match=strstr(saved_command_line, MODELNAMEEQ))) {
			pr_err("DirtySanta: \"%s\" not passed on kernel command-line\n", MODELNAMEEQ);

#ifdef CONFIG_DIRTYSANTA_FIXUP_SYSFS
			attrs[0].attr.mode|=S_IWUSR;
#endif
		} else {
			match+=strlen(MODELNAMEEQ);
			dev_name_len=strchrnul(match, ' ')-match;

			if(dev_name_len>=LG_MODEL_NAME_SIZE) {
				pr_warning("DirtySanta: model.name is longer than VENDOR0 buffer, truncating!\n");
				dev_name_len=LG_MODEL_NAME_SIZE-1;
			}

			memcpy(ds_dev_name, match, dev_name_len);
			ds_dev_name[dev_name_len]='\0';
		}
	}

	if(!sim_num) {
		if(!(match=strstr(saved_command_line, SIMNUMEQ))) {
			pr_err("DirtySanta: \"%s\" not passed on kernel command-line\n", SIMNUMEQ);

#ifdef CONFIG_DIRTYSANTA_FIXUP_SYSFS
			attrs[1].attr.mode|=S_IWUSR;
#endif
		} else {
			sim_num=match[strlen(SIMNUMEQ)]-'0';

			if(sim_num<1||sim_num>2)
				pr_warning("DirtySanta: SIM count of %d is odd\n", sim_num);
		}
	}

	pr_info("DirtySanta: values: \"%s%s\" \"%s%d\"\n", MODELNAMEEQ,
ds_dev_name, SIMNUMEQ, sim_num);
	if(ds_nt_code[0])
		pr_info("DirtySanta: values: \"nt_code=%s\"\n", ds_nt_code);

	return ret;
}


static int __init dirtysanta_fixup_msm_modem(void)
{
	struct lge_smem_vendor0 *ptr;

	unsigned size;

	if(IS_ERR_OR_NULL(ptr=smem_get_entry(SMEM_ID_VENDOR0, &size, 0, SMEM_ANY_HOST_FLAG))) {
		pr_info("DirtySanta: Qualcomm smem not initialized as of subsys_init\n");
		return -EFAULT;
	}

	if(size<sizeof(struct lge_smem_vendor0)) {
		pr_err("DirtySanta: Memory area returned by smem_get_entry() too small\n");
		return -ENOMEM;
	}

	dirtysanta_log_vendor0_summary("subsys_init before", "VENDOR0",
		ptr, size);

	if(!sim_num||!ds_dev_name[0]) {

		int ret;
		if((ret=dirtysanta_fixup_loadcfg())) return ret;

	}

	pr_info("DirtySanta: applying VENDOR0 fixup in subsys_init\n");

	dirtysanta_write_smem_string(ptr->lg_model_name, ds_dev_name,
		sizeof(ptr->lg_model_name));
	ptr->sim_num=sim_num;
	if(ds_nt_code[0])
		dirtysanta_write_smem_string(ptr->nt_code, ds_nt_code,
			sizeof(ptr->nt_code));

	dirtysanta_log_vendor0_summary("subsys_init after", "VENDOR0",
		ptr, size);

	return 0;
}

/* command-line is loaded at core_initcall() **
** smem handler is initialized at arch_initcall() */
subsys_initcall(dirtysanta_fixup_msm_modem);


#ifdef CONFIG_DIRTYSANTA_FIXUP_SYSFS
int dirtysanta_attach(struct device *dev)
{
	int i;
	for(i=0; i<ARRAY_SIZE(attrs); ++i)
		device_create_file(dev, attrs+i);

	return 1;
}
EXPORT_SYMBOL(dirtysanta_attach);


void dirtysanta_detach(struct device *dev)
{
	int i;
	for(i=0; i<ARRAY_SIZE(attrs); ++i)
		device_remove_file(dev, attrs+i);
}
EXPORT_SYMBOL(dirtysanta_detach);


static ssize_t dirtysanta_show(struct device *dev, attr_type *attr, char *buf)
{
	struct lge_smem_vendor0 *ptr;
	unsigned size;

	if(IS_ERR_OR_NULL(ptr=smem_get_entry(SMEM_ID_VENDOR0, &size, 0, SMEM_ANY_HOST_FLAG))) {
		pr_info("DirtySanta: Qualcomm smem not initialized.\n");
		return -EFAULT;
	}

	if(size<sizeof(struct lge_smem_vendor0)) {
		pr_err("DirtySanta: Memory area returned by smem_get_entry() too small\n");
		return -ENOMEM;
	}

	switch(attr-attrs) {
	case 0:
		return snprintf(buf, PAGE_SIZE, "%s\n", ptr->lg_model_name);
	case 1:
		return snprintf(buf, PAGE_SIZE, "%u\n", ptr->sim_num);
	default:
		return -EINVAL;
	}
}

static ssize_t dirtysanta_store(struct device *dev, attr_type *attr,
const char *buf, size_t len)
{
	struct lge_smem_vendor0 *ptr;
	unsigned size;

	if(IS_ERR_OR_NULL(ptr=smem_get_entry(SMEM_ID_VENDOR0, &size, 0, SMEM_ANY_HOST_FLAG))) {
		pr_info("DirtySanta: Qualcomm smem not initialized.\n");
		return -EFAULT;
	}

	if(size<sizeof(struct lge_smem_vendor0)) {
		pr_err("DirtySanta: Memory area returned by smem_get_entry() too small\n");
		return -ENOMEM;
	}

	switch(attr-attrs) {
		int rc, cnt;
		size_t use;
	case 0:
		/* filter out this extreme case early */
		if(len>=(LG_MODEL_NAME_SIZE+LG_MODEL_NAME_SIZE/2)) {
			pr_notice("DirtySanta: Model name is too long\n");
			return -EINVAL;
		}

		/* most likely \n, but an extra \0 can be caught too */
		for(use=0; use<len; ++use) if(!isprint(buf[use]))
			break;

		if(use>=LG_MODEL_NAME_SIZE) {
			pr_notice("DirtySanta: Model name is too long\n");
			return -EINVAL;
		}
		if(strncmp("LG-", buf, 3))
			pr_notice("DirtySanta: Model name is unusual\n");

		dirtysanta_write_smem_string(ptr->lg_model_name, buf, use+1);
		pr_info("DirtySanta: Modem name \"%s\"\n", ptr->lg_model_name);

		return len;

	case 1:
		if((rc=kstrtoint(buf, 0, &cnt))) return rc;
		if(cnt<=0) {
			pr_notice("DirtySanta: Got zero/negative SIM count: %d\n", cnt);
			return -EINVAL;
		} else if(cnt>2) {
			if(cnt>4) {
				pr_notice("DirtySanta: Got excessive SIM count: %d\n", cnt);
				return -EINVAL;
			}
			pr_info("DirtySanta: Got unusually high SIM count\n");
		}

		pr_info("DirtySanta: SIM count %d\n", cnt);
		ptr->sim_num=cnt;
		return len;

	default:
		return -EINVAL;
	}
}
#endif // CONFIG_DIRTYSANTA_FIXUP_SYSFS
