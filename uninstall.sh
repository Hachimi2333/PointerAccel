#!/usr/bin/env bash
set -euo pipefail
pa_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$pa_source/scripts/paths.sh"
pa_check_paths
pa_offline=false;pa_system_test=()
while (($#)); do
    case "$1" in
    --offline) pa_offline=true;shift;;
    --test-system) pa_system_test=(--test-root "${2:?}");shift 2;;
    *) pa_error "未知参数: $1";exit 2;;
    esac
done
if [[ ! -e $pa_manifest ]]; then
    [[ ! -e $pa_data && ! -e $pa_state ]] || { pa_error '安装不完整或清单缺失；停止以避免误删。';exit 1; }
    printf 'PointerAccel 未安装。\n';exit 0
fi
pa_verify_manifest
pa_program="$pa_data/bin/pointeraccel"
if ! $pa_offline; then
    if ! "$pa_program" internal-shutdown >/dev/null 2>&1; then
        if gdbus call --session --dest org.kde.KWin --object-path /org/kde/KWin/PointerAccel --method org.kde.KWin.PointerAccel1.Status >/dev/null 2>&1; then
            pa_error '模块仍在运行且关闭失败，已停止卸载。';exit 1
        fi
    fi
fi
pa_hash=$(cat "$pa_state/system.sha256")
"$pa_program" internal-plugin restore "$pa_state/backups/kwin.json" "$(cat "$pa_state/autostart.sha256")"
if ((${#pa_system_test[@]})); then bash "$pa_data/scripts/system-plugin.sh" "${pa_system_test[@]}" remove "$pa_uid" "$pa_hash"
else sudo -- bash "$pa_data/scripts/system-plugin.sh" remove "$pa_uid" "$pa_hash";fi
if [[ -f "$pa_state/backups/config.before" ]]; then cp -p -- "$pa_state/backups/config.before" "$pa_config/config.json"
else rm -f -- "$pa_config/config.json";fi
pa_files=();pa_directories=()
while IFS=$'\t' read -r pa_kind pa_value pa_path; do
    if [[ $pa_kind == F || $pa_kind == L ]]; then pa_files+=("$pa_path");
    elif [[ $pa_kind == D ]]; then pa_directories+=("$pa_path");fi
done < "$pa_manifest"
for pa_path in "${pa_files[@]}"; do rm -- "$pa_path";done
rm -- "$pa_state/autostart.sha256" "$pa_manifest"
for pa_path in "${pa_directories[@]}"; do rmdir -- "$pa_path" 2>/dev/null || true;done
# Older installations may have gained child directories in a subsequent update.
for pa_path in "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_state/backups" "$pa_data" "$pa_state"; do rmdir -- "$pa_path" 2>/dev/null || true;done
printf '已恢复原输入、自启和配置，并删除安装清单中的文件。\n请手动重新登录，以清除当前 KWin 内存中可能驻留的模块。\n'
