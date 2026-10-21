# Wedge root-cause status 2026-10-21 (post-experiment program)

## Complete proven model
- Wedge = RTL8382 SoC loses its timer/clockevent interrupt; CPU idles in
  WAIT forever; all tasks sleep, never wake. IRQ-driven paths live
  (printk/journal, ICMP, TCP SYN-ACK, link flaps). Proven by: sysrq-p
  (epc=__r4k_wait), sysrq-q (hrtimer_wakeup pending +14ms never fires),
  sysrq-t (53 tasks all sleeping, zero D-state), BREAK+b recovery works
  from IRQ context (validated 3x, zero bench visits).
- Trigger = `ip route add 192.168.13.0/24 dev switch.1008 scope link`
  + sustained .13-sourced traffic (ssh/ping/monitor). Route alone in
  silence: SAFE (4+ min, tested). One burst: wedge < 15 s.
- The kernel route-add itself completes successfully (strace: netlink
  ACK error=0, exit 0). The driver fib4 path FILTERS this route before
  any hardware write (breadcrumbs: no router_mac_guard line, no
  hashtable-extended at killer add). No HW LPM entry is needed.
- The rtl83xx netevent/neigh-offload path is innocent (notifier
  early-returns for switch.1008 events; works that run are lookup-miss
  no-ops).

## Controlled experiment ladder (E1-E7, 2026-10-21)
| # | Setup | Result |
|---|-------|--------|
| E1 | unicast TX to unknown fake MAC out switch.1008 | SAFE |
| E2 | broadcast/ARP egress out switch.1008 (2 frames) | SAFE |
| E4 | unicast to LIVE cross-VLAN MAC (ERX), 3 frames | SAFE |
| E6 | sustained unicast (50x) to fake static MAC | SAFE |
| E5 | route + static .13 neigh (ERX MAC) + live .13 ssh | WEDGE (on contact) |
| L3 | route only, total silence | SAFE 4+ min |
| orig | route + sustained .13 traffic (no manual neigh) | WEDGE <=15 s |

## Remaining open fork (next session, one experiment each)
A. sustained ARP-broadcast probing on switch.1008 (kernel probes for
   .13 hosts, ~3 s cadence) jams ASIC — test: route + inbound .13
   traffic with iptables drop (kills reply/probe TX side).
B. inbound .13-SOURCED packets with conflicting route present trip a
   hardware uRPF/L3 ingress path — test: route + inbound-only dropped
   traffic vs route + probes suppressed.

## Fix design (do not ship blind - 5 wrong theories this week)
1. Driver guard in rtl838x_eth_tx: on CPU-injected DSA frames, check
   dst MAC FDB membership vs egress VLAN/port; drop on conflict.
   Needs cross-module helper (rtl83xx FDB read) - both built-in.
2. CPU-tag L2/bypass bits in create_tx_header (rtl838x_eth.c:94) -
   REQUIRES datasheet/SDK confirmation of bit positions first.
3. Operational (shipped already): tools/svi_preflight.sh refuses the
   overlapping route class entirely.

## Instrument kit (all validated on-box)
- wedgedump v4 (/root/.hermes/cache/scratch/wedgedump4.sh) - rc.local
  prober+dumper, no stat/console deps (v1-v3 lessons in script header)
- BREAK+<key> over local FTDI = IRQ-context sysrq works DURING wedge:
  t=task dump, p=registers, q=timers, b=reboot. Serial input via
  getty dies; BREAK path lives.
- Breadcrumb kernel build: files-6.12 patch (scratch/bc-round2.sh on
  top of breadcrumb-patch.sh), 12 pr_info WC: markers, journal-visible.
- Repro: 3 commands (alias + route + ping), BREAK+b to recover.
