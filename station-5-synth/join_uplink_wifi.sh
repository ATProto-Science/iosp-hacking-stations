#!/usr/bin/env bash
# Station 5: point the noizetoyz OpenWrt router at an upstream WiFi network
# for internet access, without needing an Ethernet uplink — the "Primary
# plan" bridge described in README.md's "Workshop network topology" section.
#
# What this does on the router (all via uci, no package installs — the
# device has ~90KB free on its overlay flash, no room for relayd):
#   - adds a station (client) wifi-iface on radio0, joining the given SSID,
#     network 'wwan'
#   - adds network.wwan as a plain DHCP interface
#   - adds 'wwan' to the existing firewall wan zone (reuses its masq rule)
#   - reloads wifi + firewall
#
# This is a *routed* NAT bridge, not a transparent L2 one: noizetoyz clients
# stay on 192.168.1.0/24 and get NATed out through wwan, they don't join the
# upstream network's own subnet. Verified against a FRITZ!Box on 2026-09-03.
#
# Gotcha: this device has exactly one radio. Once the station interface
# associates, mac80211 forces every other interface on that radio onto the
# same channel — so the noizetoyz AP's channel will jump to match whatever
# channel the upstream network is on. Anyone with noizetoyz already saved
# may need to reconnect once. There's no way around this with one radio.
#
# Idempotent: safe to re-run with a new SSID/password (e.g. switching from
# a home test network to the actual venue WiFi at IOSP) — updates the
# existing wwan_sta section and firewall list entry in place rather than
# duplicating them.
#
# Usage:
#   ./join_uplink_wifi.sh "<SSID>" "<password>" [router-ssh-host]
#
# router-ssh-host defaults to root@192.168.1.1 (this station's OpenWrt LAN
# address). Override if the router's IP differs from the last-verified lab
# setup.
#
# Alternative: LuCI (http://192.168.1.1/, or whatever the router's current
# LAN IP is) has a network-scan-and-join wizard under Network > Wireless >
# Scan, useful at the venue if the exact SSID isn't known ahead of time.
# Reach for this script instead when the SSID/password are already known
# and you want it scripted/repeatable (e.g. re-applying after a config
# wipe, or switching between a home test network and the venue one).

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <SSID> <password> [router-ssh-host]" >&2
  exit 1
fi

SSID="$1"
WIFI_PASSWORD="$2"
ROUTER="${3:-root@192.168.1.1}"

# OpenWrt 18.06's dropbear only offers ssh-rsa host/user keys, which modern
# OpenSSH clients refuse by default — needed on every connection to this
# router, not just here (see also station-5-synth/README.md).
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new
          -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa)

# Safely single-quote a value for embedding in the remote sh script below —
# handles SSID/passwords containing '$', '"', backticks, etc. Only a literal
# single quote needs special handling for sh's quoting rules.
sq() { printf "%s" "$1" | sed "s/'/'\\\\''/g"; }
SSID_Q=$(sq "$SSID")
KEY_Q=$(sq "$WIFI_PASSWORD")

echo "[join_uplink_wifi] configuring $ROUTER to join '$SSID' as uplink..."

ssh "${SSH_OPTS[@]}" "$ROUTER" sh -s <<REMOTE
set -e

uci set network.wwan='interface'
uci set network.wwan.proto='dhcp'
uci commit network

uci set wireless.wwan_sta='wifi-iface'
uci set wireless.wwan_sta.device='radio0'
uci set wireless.wwan_sta.network='wwan'
uci set wireless.wwan_sta.mode='sta'
uci set wireless.wwan_sta.ssid='$SSID_Q'
uci set wireless.wwan_sta.encryption='psk2'
uci set wireless.wwan_sta.key='$KEY_Q'
uci commit wireless

uci del_list firewall.@zone[1].network='wwan' 2>/dev/null || true
uci add_list firewall.@zone[1].network='wwan'
uci commit firewall

echo "--- reloading wifi + firewall ---"
wifi reload
/etc/init.d/firewall restart >/dev/null 2>&1
sleep 8

echo "--- station link ---"
iwinfo wlan0 info 2>/dev/null | grep -E "ESSID|Signal|Mode" || true
echo "--- wwan address ---"
ip -4 addr show dev wwan 2>/dev/null || ubus call network.interface.wwan status 2>/dev/null | grep -E '"address"|"ipv4-address"' || true
echo "--- internet check ---"
ping -c2 -W2 8.8.8.8 || echo "no reply yet — DHCP/association may still be settling, check again in a few seconds"
REMOTE

echo
echo "[join_uplink_wifi] done. Re-verify from a LAN client (e.g. robopi) with:"
echo "  ssh pi@re.lan 'curl -s https://icanhazip.com; ping -c2 8.8.8.8'"
