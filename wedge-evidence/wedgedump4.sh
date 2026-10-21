#!/bin/sh
# wedge-dump v4: stat does not exist on this box (caused all false fires).
# Prober WRITES epoch into the heartbeat file; dumper reads it via cat.
HB=/tmp/rtnl-hb
date +%s > $HB

prober() {
  while :; do
    if ip route get 8.8.8.8 >/dev/null 2>&1; then
      date +%s > $HB
    fi
    sleep 5
  done
}

dumper() {
  STALE=0
  sleep 90
  while :; do
    sleep 10
    NOW=$(date +%s)
    LAST=$(cat $HB 2>/dev/null || echo $NOW)
    AGE=$(( NOW - LAST ))
    [ $AGE -gt 45 ] && STALE=$((STALE+1)) || STALE=0
    if [ $STALE -ge 2 ]; then
      echo t > /proc/sysrq-trigger 2>/dev/null
      sleep 5
      echo w > /proc/sysrq-trigger 2>/dev/null
      STALE=0
      sleep 120
    fi
  done
}

case "$1" in
  watcher-only) ;;
  *)
    prober &
    ;;
esac
dumper &

cp "$0" /usr/bin/wedgedump.sh 2>/dev/null
grep -q wedgedump /etc/rc.local 2>/dev/null || \
  sed -i '/^exit 0/i sh /usr/bin/wedgedump.sh' /etc/rc.local 2>/dev/null
