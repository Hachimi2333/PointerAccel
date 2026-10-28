#!/usr/bin/env bash
set -euo pipefail
pa_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$pa_source/scripts/paths.sh"
pa_check_paths
pa_build="$pa_source/build"
pa_system_test=()
while (($#)); do
    case "$1" in
    --test-system) pa_system_test=(--test-root "${2:?}");shift 2;;
    --build-dir) pa_build=${2:?};shift 2;;
    *) pa_error "未知参数: $1";exit 2;;
    esac
done
for pa_tool in cmake ninja g++ pkg-config; do command -v "$pa_tool" >/dev/null || { pa_error "缺少依赖: $pa_tool";exit 1; };done
if [[ ${POINTERACCEL_TEST_NO_BUILD:-0} != 1 || ${#pa_system_test[@]} == 0 ]]; then
    cmake -S "$pa_source" -B "$pa_build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
    cmake --build "$pa_build" --parallel "${POINTERACCEL_BUILD_JOBS:-4}"
    ctest --test-dir "$pa_build" --output-on-failure
fi
if [[ -e $pa_manifest ]]; then pa_verify_manifest
else
    for pa_path in "$pa_data" "$pa_state"; do [[ ! -e $pa_path ]] || { pa_error "目录已存在且没有安装清单: $pa_path";exit 1; };done
    [[ ! -e $pa_cli && ! -L $pa_cli ]] || { pa_error "CLI 路径已被占用: $pa_cli";exit 1; }
fi
pa_backup=$(mktemp -d)
pa_mutated=false;pa_system_changed=false;pa_success=false;pa_new_dirs=()
pa_names=(data config state cli kwin)
pa_paths=("$pa_data" "$pa_config" "$pa_state" "$pa_cli" "$pa_kwin")
for pa_i in "${!pa_paths[@]}"; do
    if [[ -e ${pa_paths[$pa_i]} || -L ${pa_paths[$pa_i]} ]]; then cp -a -- "${pa_paths[$pa_i]}" "$pa_backup/${pa_names[$pa_i]}";fi
done
pa_system_call() {
    if ((${#pa_system_test[@]})); then bash "$pa_source/scripts/system-plugin.sh" "${pa_system_test[@]}" "$@"
    else sudo -- bash "$pa_source/scripts/system-plugin.sh" "$@";fi
}
pa_finish() {
    local result=$?
    if ! $pa_success; then
        if $pa_system_changed; then pa_system_call rollback "$pa_uid" || pa_error '系统回滚未完成，请保留终端输出。';fi
        if $pa_mutated; then
            for pa_i in "${!pa_paths[@]}"; do
                rm -rf -- "${pa_paths[$pa_i]}"
                if [[ -e "$pa_backup/${pa_names[$pa_i]}" || -L "$pa_backup/${pa_names[$pa_i]}" ]]; then
                    mkdir -p -- "${pa_paths[$pa_i]%/*}";cp -a -- "$pa_backup/${pa_names[$pa_i]}" "${pa_paths[$pa_i]}"
                fi
            done
        fi
        for pa_path in "${pa_new_dirs[@]}"; do rmdir -- "$pa_path" 2>/dev/null || true;done
    fi
    rm -rf -- "$pa_backup"
    return "$result"
}
trap pa_finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
pa_new_dirs=()
for pa_path in "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_config" "$pa_state/backups" "${pa_cli%/*}"; do
    pa_parent=$pa_path
    while [[ ! -d $pa_parent ]]; do pa_new_dirs+=("$pa_parent");pa_parent=${pa_parent%/*};done
done
pa_mutated=true
mkdir -p -- "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_config" "$pa_state/backups" "${pa_cli%/*}"
chmod 0700 "$pa_state" "$pa_state/backups"
if [[ ! -f "$pa_state/backups/kwin.json" ]]; then
    "$pa_build/pointeraccel" internal-plugin backup "$pa_state/backups/kwin.json"
    if [[ -f "$pa_config/config.json" ]]; then cp -p -- "$pa_config/config.json" "$pa_state/backups/config.before";fi
fi
install -m0755 -- "$pa_build/pointeraccel" "$pa_data/bin/pointeraccel"
install -m0755 -- "$pa_build/pointeraccel-raw-wayland" "$pa_data/bin/pointeraccel-raw-wayland"
install -m0755 -- "$pa_build/pointeraccel-raw-x11" "$pa_data/bin/pointeraccel-raw-x11"
install -m0755 -- "$pa_source/uninstall.sh" "$pa_data/uninstall.sh"
install -m0644 -- "$pa_source/scripts/paths.sh" "$pa_data/scripts/paths.sh"
install -m0755 -- "$pa_source/scripts/system-plugin.sh" "$pa_data/scripts/system-plugin.sh"
install -m0644 -- "$pa_source/README.md" "$pa_data/README.md"
install -m0644 -- "$pa_source/LICENSE" "$pa_data/licenses/PointerAccel.txt"
install -m0644 -- "$pa_source/LICENSES/windows-pointer-linux.txt" "$pa_data/licenses/windows-pointer-linux.txt"
"$pa_data/bin/pointeraccel" internal-init-config
ln -sfn -- "$pa_data/bin/pointeraccel" "$pa_cli"
printf '%s\n' "$pa_uid" > "$pa_state/owner"
pa_module="$pa_build/stage/qt6/plugins/kwin/plugins/pointeraccel_kwin.so"
pa_sha=$(sha256sum -- "$pa_module");pa_sha=${pa_sha%% *}
pa_system_call install "$pa_uid" "$pa_module" "$pa_sha"
pa_system_changed=true
if ((${#pa_system_test[@]})) && [[ ${POINTERACCEL_TEST_FAIL_AFTER_MODULE:-0} == 1 ]]; then false;fi
printf '%s\n' "$pa_sha" > "$pa_state/system.sha256"
"$pa_data/bin/pointeraccel" internal-plugin enable > "$pa_state/autostart.sha256"
pa_manifest_new="$pa_state/manifest.new"
: > "$pa_manifest_new"
for pa_path in "$pa_data/bin/pointeraccel" "$pa_data/bin/pointeraccel-raw-wayland" "$pa_data/bin/pointeraccel-raw-x11" "$pa_data/uninstall.sh" "$pa_data/scripts/paths.sh" "$pa_data/scripts/system-plugin.sh" "$pa_data/README.md" "$pa_data/licenses/PointerAccel.txt" "$pa_data/licenses/windows-pointer-linux.txt" "$pa_state/owner" "$pa_state/system.sha256" "$pa_state/backups/kwin.json" "$pa_state/backups/config.before"; do
    if [[ -f $pa_path ]]; then pa_sha=$(sha256sum -- "$pa_path");printf 'F\t%s\t%s\n' "${pa_sha%% *}" "$pa_path" >> "$pa_manifest_new";fi
done
printf 'L\t%s\t%s\n' "$pa_data/bin/pointeraccel" "$pa_cli" >> "$pa_manifest_new"
if [[ -f "$pa_backup/state/manifest.tsv" ]]; then
    while IFS=$'\t' read -r pa_kind pa_value pa_path; do [[ $pa_kind != D ]] || printf 'D\t-\t%s\n' "$pa_path" >> "$pa_manifest_new";done < "$pa_backup/state/manifest.tsv"
fi
for pa_path in "${pa_new_dirs[@]}"; do printf 'D\t-\t%s\n' "$pa_path" >> "$pa_manifest_new";done
mv -- "$pa_manifest_new" "$pa_manifest"
pa_verify_manifest
pa_system_call commit "$pa_uid"
pa_success=true
printf '\n安装完成。请保存工作并手动重新登录 Plasma。当前桌面不会被重启。\nCLI: %s\n卸载: %s/uninstall.sh\n' "$pa_cli" "$pa_data"
