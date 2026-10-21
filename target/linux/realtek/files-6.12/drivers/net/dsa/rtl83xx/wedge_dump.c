// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wedge_dump - IRQ-context hardware register dumper for rtl838x wedge
 * forensics. Registered on sysrq key 'y' (BREAK+y over serial works
 * during the wedge: sysrq runs from interrupt context; printk reaches
 * the journal through the only path that survives).
 *
 * Reads: MIPS CPU IRQ mask, SoC interrupt controller, RTL838x timer
 * block, switch-core global interrupt status. Raw values only.
 */
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/hrtimer.h>
#include <linux/sched/idle.h>
#include <linux/smp.h>


#include <linux/module.h>

#define RTL_BASE(n) ((void __iomem *)CKSEG1ADDR(0x18000000 + (n)))

static void wedge_dump_handler(u8 key)
{
	int i;
	pr_info("WEDGE-DUMP: cpu_irq_mask=%08x\n",
		readl(RTL_BASE(0x3000)));          /* RTL838x IRQ controller */
	for (i = 0; i < 8; i++)
		pr_info("WEDGE-DUMP: irq_ctrl[%02x]=%08x\n", 0x3008 + i * 4,
			readl(RTL_BASE(0x3008 + i * 4)));
	for (i = 0; i < 4; i++)
		pr_info("WEDGE-DUMP: timer_blk[%02x]=%08x\n", 0x3100 + i * 4,
			readl(RTL_BASE(0x3100 + i * 4)));
	for (i = 0; i < 4; i++)
		pr_info("WEDGE-DUMP: sw_glb_isr[%02x]=%08x\n", 0x7000 + i * 4,
			readl(RTL_BASE(0x7000 + i * 4)));

	{
		u32 cnt, cmp, cause, status;
		__asm__ __volatile__(
			"mfc0 %0, $9, 0\n\t"
			"mfc0 %1, $11, 0\n\t"
			"mfc0 %2, $13, 0\n\t"
			"mfc0 %3, $12, 0\n\t"
			: "=r"(cnt), "=r"(cmp), "=r"(cause), "=r"(status));
		pr_info("WEDGE-DUMP: cp0_count=%08x cp0_compare=%08x\n", cnt, cmp);
		pr_info("WEDGE-DUMP: cp0_cause=%08x cp0_status=%08x\n", cause, status);
	}

	{
		unsigned int irq;
		for (irq = 1; irq < nr_irqs; irq++) {
			unsigned int c = kstat_irqs_usr(irq);
			if (c)
				pr_info("WEDGE-DUMP: irq[%u]=%u\n", irq, c);
		}
	}
	for (i = 0; i < 12; i++)
		pr_info("WEDGE-DUMP: irq_all[%02x]=%08x\n", 0x3000 + i * 4,
			readl(RTL_BASE(0x3000 + i * 4)));
	for (i = 0; i < 8; i++)
		pr_info("WEDGE-DUMP: tc0[%02x]=%08x\n", 0x3100 + i * 4,
			readl(RTL_BASE(0x3100 + i * 4)));


	{
		struct task_struct *t;
		int running = 0, unint = 0, sleeping = 0;
		rcu_read_lock();
		for_each_process(t) {
			if (t->__state == TASK_RUNNING)
				running++;
			else if (t->__state & TASK_UNINTERRUPTIBLE)
				unint++;
			else
				sleeping++;
		}
		rcu_read_unlock();
		pr_info("WEDGE-DUMP: tasks running=%d unint=%d sleeping=%d curr=%s\n",
			running, unint, sleeping, current->comm);
	}

	{
		u64 t1 = ktime_get_mono_fast_ns();
		u32 c1, c2;
		__asm__ __volatile__("mfc0 %0, $9, 0" : "=r"(c1));
		pr_info("WEDGE-DUMP: mono_ns=%llu cp0_count=%08x\n", t1, c1);
		__asm__ __volatile__("mfc0 %0, $9, 0" : "=r"(c2));
		pr_info("WEDGE-DUMP: cp0_count_again=%08x\n", c2);
	}
	pr_info("WEDGE-DUMP: done\n");
}

static struct sysrq_key_op wedge_dump_op = {
	.handler = wedge_dump_handler,
	.help_msg = "wedge-dump(y)",
	.action_msg = "WEDGE-DUMP: SoC register snapshot",
};

static int __init wedge_dump_init(void)
{
	return register_sysrq_key('y', &wedge_dump_op);
}
module_init(wedge_dump_init);
MODULE_LICENSE("GPL");
