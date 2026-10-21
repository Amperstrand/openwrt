#!/bin/sh
# Round-2 breadcrumbs: inside the notifier tail + work entry (on top of round-1).
set -e
D=~/wt-rtl-wdt/target/linux/realtek/files-6.12/drivers/net/dsa/rtl83xx
cd "$D"

# notifier: the nud filter print gets the neighbor IP; tail gets per-line marks
perl -0pi -e 's|\t\tpr_info\("WC: neigh_update dev=%s nud=%x\\\\n", n->dev->name, n->nud_state\);|\t\tpr_info("WC: neigh_update dev=%s nud=%x ip=%pI4\\\\n", n->dev->name, n->nud_state, (u32 *)n->primary_key);|' common.c
perl -0pi -e 's|\t\tnet_work = kzalloc\(sizeof\(\*net_work\), GFP_ATOMIC\);\n\t\tif \(!net_work\)\n\t\t\treturn NOTIFY_BAD;|\t\tnet_work = kzalloc(sizeof(*net_work), GFP_ATOMIC);\n\t\tif (!net_work)\n\t\t\treturn NOTIFY_BAD;\n\t\tpr_info("WC: nw_alloc ok ip=%pI4 port=%d\\\\n", (u32 *)n->primary_key, port);|' common.c
perl -0pi -e 's|\t\tqueue_work\(priv->wq, &net_work->work\);|\t\tpr_info("WC: nw_queue mac=%016llx wq=%ps\\\\n", net_work->mac, priv->wq);\n\t\tqueue_work(priv->wq, \&net_work->work);|' common.c

# work entry: first line of net_event_work_do
perl -0pi -e 's|(\tstruct rtl838x_switch_priv \*priv = net_work->priv;\n)\n\trtl83xx_l3_nexthop_update|$1\tpr_info("WC: nw_do_enter ip=%pI4 mac=%016llx\\\\n", \&net_work->gw_addr, net_work->mac);\n\n\trtl83xx_l3_nexthop_update|' common.c

# l3_nexthop_update: entry print BEFORE rcu_read_lock (catch even early death)
perl -0pi -e 's|\trcu_read_lock\(\);\n\tpr_info\("WC: nh_update ip=%pI4 mac=%016llx\\\\n", &ip_addr, mac\);|\tpr_info("WC: nh_update(2) ip=%pI4 mac=%016llx\\\\n", \&ip_addr, mac);\n\trcu_read_lock();|' common.c

grep -c "WC:" common.c
echo ROUND2-OK
