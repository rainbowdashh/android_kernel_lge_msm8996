// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <soc/qcom/lge/board_lge.h>

static int cmdline_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_LGE_USB_FACTORY
	if (lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO) {
		const char *arg = saved_command_line;
		const char *end;

		/* Replace complete mode arguments without changing the saved line. */
		while (*arg) {
			end = strchrnul(arg, ' ');
			if (strncmp(arg, "androidboot.mode=",
				    sizeof("androidboot.mode=") - 1))
				seq_write(m, arg, end - arg + !!*end);
			arg = end + !!*end;
		}
		seq_puts(m, " androidboot.mode=charger\n");
		return 0;
	}
#endif
	seq_puts(m, saved_command_line);
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_cmdline_init(void)
{
	proc_create_single("cmdline", 0, NULL, cmdline_proc_show);
	return 0;
}
fs_initcall(proc_cmdline_init);
