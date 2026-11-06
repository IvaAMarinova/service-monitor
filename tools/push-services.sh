#!/usr/bin/env bash
# Upload or download the service list over Wi-Fi.
#
#   ./tools/push-services.sh                    upload tools/services.txt
#   ./tools/push-services.sh my-list.txt        upload another file
#   ./tools/push-services.sh --get              print the board's current list
#   ./tools/push-services.sh --reset            restore the firmware defaults
#
# Board address: service-monitor.local, or set SERVICE_MONITOR_HOST=192.168.1.42
# If OTA_PASSWORD is set in secrets.h, export SERVICE_MONITOR_OTA_PASSWORD too.
set -euo pipefail

host="${SERVICE_MONITOR_HOST:-service-monitor.local}"
auth=()
if [[ -n "${SERVICE_MONITOR_OTA_PASSWORD:-}" ]]; then
  auth=(--user "admin:${SERVICE_MONITOR_OTA_PASSWORD}")
fi
curl_board() { curl --fail-with-body --silent --show-error --max-time 10 ${auth[@]+"${auth[@]}"} "$@"; }

case "${1:-}" in
  --get)   curl_board "http://${host}/services" ;;
  --reset) curl_board -X POST "http://${host}/services/reset" ;;
  -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' ;;
  *)
    file="${1:-$(dirname "$0")/services.txt}"
    [[ -f "$file" ]] || { echo "no such file: $file" >&2; exit 1; }
    curl_board -X PUT -H 'Content-Type: text/plain' --data-binary "@${file}" "http://${host}/services"
    ;;
esac
