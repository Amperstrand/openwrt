#!/bin/sh
# Add pr_info breadcrumbs to the rtl83xx L3 paths (instrumented build).
# Runs in the kernel source dir on ai-legion-small. Idempotent-ish: run once.
set -e
D=~/wt-rtl-wdt/build_dir/target-mips_24kc_musl/linux-realtek_rtl838x/linux-6.12.87/drivers/net/dsa/rtl83xx
cd "$D"
cp common.c common.c.orig-wedge5
cp rtl838x.c rtl838x.c.orig-wedge5

# 1. fib4_add entry + post-alloc + router-mac guard
sed -i 's|\tif (rtldsa_fib4_check(priv, info, FIB_EVENT_ENTRY_ADD))|\tpr_info("WC: fib4_add dst=%pI4/%d ndev=%s vlan=%d\\\\n", \&info->dst, info->dst_len, ndev->name, vlan);\n\tif (rtldsa_fib4_check(priv, info, FIB_EVENT_ENTRY_ADD))|' common.c
sed -i 's|\tif (route)\n\t\tdev_info|XX|' common.c
perl -0pi -e 's|(\tif \(route\))\n(\t\tdev_info priv->dev, "route hashtable)|$1\n\t\tdev_info(priv->dev, "WC: route_alloc idx=%d", route->id);\n$2|s' common.c || true
perl -0pi -e 's|\tif \(priv->r->set_l3_router_mac\) \{|\tpr_info("WC: router_mac_guard=%d gw=%pI4\\\\n", !!priv->r->set_l3_router_mac, \&nh->fib_nh_gw4);\n\tif (priv->r->set_l3_router_mac) {|' common.c

# 2. netevent entry
perl -0pi -e 's|\tcase NETEVENT_NEIGH_UPDATE:|\tcase NETEVENT_NEIGH_UPDATE:\n\t\tpr_info("WC: neigh_update dev=%s nud=%x\\\\n", n->dev->name, n->nud_state);|' common.c

# 3. l3_nexthop_update: entry + lookup result + each write guard
perl -0pi -e 's|\trcu_read_lock\(\);\n\tlist = rhltable_lookup\(&priv->routes, &ip_addr, route_ht_params\);|\trcu_read_lock();\n\tpr_info("WC: nh_update ip=%pI4 mac=%016llx\\\\n", \&ip_addr, mac);\n\tlist = rhltable_lookup(&priv->routes, \&ip_addr, route_ht_params);\n\tpr_info("WC: nh_lookup_hit=%d\\\\n", !!list);|' common.c
perl -0pi -e 's|\t\tif \(priv->r->set_l3_egress_mac\)\n\t\t\tpriv->r->set_l3_egress_mac\(r->id, mac\);|\t\tpr_info("WC: pre_egress_mac id=%d guard=%d\\\\n", r->id, !!priv->r->set_l3_egress_mac);\n\t\tif (priv->r->set_l3_egress_mac)\n\t\t\tpriv->r->set_l3_egress_mac(r->id, mac);|' common.c
perl -0pi -e 's|\t\t\t\} else \{\n\t\t\t\tpriv->r->route_write\(r->id, r\);|\t\t\t\tpr_info("WC: pre_route_write id=%d l2_id=%d\\\\n", r->id, r->nh.l2_id);\n\t\t\t\tpriv->r->route_write(r->id, r);|' common.c
perl -0pi -e 's|\t\tif \(priv->r->set_l3_nexthop\)\n\t\t\tpriv->r->set_l3_nexthop\(r->nh.id, r->nh.l2_id, r->nh.if_id\);\n\n\t\tif \(r->pr.id < 0\) \{|\t\tpr_info("WC: pre_set_l3_nexthop id=%d guard=%d\\\\n", r->nh.id, !!priv->r->set_l3_nexthop);\n\t\tif (priv->r->set_l3_nexthop)\n\t\t\tpriv->r->set_l3_nexthop(r->nh.id, r->nh.l2_id, r->nh.if_id);\n\n\t\tif (r->pr.id < 0) {|' common.c

# 4. route_write + pie_rule_write in rtl838x.c: log idx + table regs
perl -0pi -e 's|\tstruct table_reg \*r = rtl_table_get\(RTL8380_TBL_1, 2\);\n|\tstruct table_reg *r = rtl_table_get(RTL8380_TBL_1, 2);\n\tpr_info("WC: route_write idx=%d gw=%llx\\\\n", idx, rt->nh.gw);|' rtl838x.c

grep -c "WC:" common.c rtl838x.c
echo PATCHED-OK
