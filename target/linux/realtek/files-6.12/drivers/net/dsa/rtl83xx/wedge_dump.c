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
#include <linux/preempt.h>
#include <linux/ftrace.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/idle.h>
#include <linux/hrtimer.h>
#include <linux/kallsyms.h>
#include <linux/clockchips.h>
#include <linux/list.h>
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

	{
		struct list_head *head;
		struct list_head *pos;
		unsigned long addr = kallsyms_lookup_name("clockevent_devices");

		if (!addr) {
			pr_info("WEDGE-DUMP: kallsyms miss clockevent_devices\n");
		} else {
			head = (struct list_head *)addr;
			if (0) list_for_each(pos, head) {
				struct clock_event_device *dev;
				char sym[KSYM_SYMBOL_LEN];
				dev = list_entry(pos, struct clock_event_device, list);
				sprint_symbol(sym, (unsigned long)dev->event_handler);
				pr_info("WEDGE-DUMP: ce=%s state=%u handler=%s features=%08x\n",
					dev->name ? dev->name : "?",
					dev->state_use_accessors, sym, dev->features);
			}
		}
		addr = kallsyms_lookup_name("hrtimer_interrupt");
		pr_info("WEDGE-DUMP: hrtimer_interrupt=%px\n", (void *)addr);
	}

	{
		unsigned long td_addr = kallsyms_lookup_name("tick_cpu_device");
		unsigned long *poff = (unsigned long *)kallsyms_lookup_name("__per_cpu_offset");

		if (td_addr && poff) {
			/* mirror of struct tick_device layout (ptr, int) */
			struct tick_probe { struct clock_event_device *evtdev; int mode; };
			struct tick_probe *td = (struct tick_probe *)(td_addr + poff[0]);
			char sym[KSYM_SYMBOL_LEN];
			u64 now = ktime_get_mono_fast_ns();

			sprint_symbol(sym, (unsigned long)td->evtdev->event_handler);
			pr_info("WEDGE-DUMP: tickdev mode=%d evtdev=%s state=%u\n",
				td->mode, td->evtdev->name ? td->evtdev->name : "?",
				td->evtdev->state_use_accessors);
			pr_info("WEDGE-DUMP: next_event=%llu now=%llu delta=%lld\n",
				td->evtdev->next_event, now,
				(s64)td->evtdev->next_event - (s64)now);
			pr_info("WEDGE-DUMP: handler=%s\n", sym);
		} else {
			pr_info("WEDGE-DUMP: tick probe kallsyms miss td=%px off=%px\n",
				(void *)td_addr, (void *)poff);
		}
	}

	{
		extern unsigned int rtl_wedge_softirq_pending(void);
		pr_info("WEDGE-DUMP: softirq_pending=%08x preempt_count=%x in_softirq=%d\n",
			local_softirq_pending(), preempt_count(),
			(int)in_softirq());
	}

	{
		struct task_struct *idle = idle_task(0);
		pr_info("WEDGE-DUMP: idle_need_resched=%d idle_state=%lx curr=%s\n",
			test_tsk_need_resched(idle), (unsigned long)idle->__state,
			current->comm);
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
