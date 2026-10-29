#!/usr/bin/env bash
set -euo pipefail
pa_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$pa_source/scripts/paths.sh"
pa_check_paths
if [[ ${POINTERACCEL_MAINTENANCE_LOCKED:-} != "$pa_state" ]]; then
    exec python3 "$pa_source/scripts/maintenance.py" locked bash "$pa_source/install.sh" "$@"
fi
pa_build="$pa_source/build"
pa_system_test=()
pa_recover=false
while (($#)); do
    case "$1" in
    --test-system) pa_system_test=(--test-root "${2:?}");shift 2;;
    --build-dir) pa_build=${2:?};shift 2;;
    --recover) pa_recover=true;shift;;
    *) pa_error "未知参数: $1";exit 2;;
    esac
done
pa_system_call() {
    if ((${#pa_system_test[@]})); then bash "$pa_source/scripts/system-plugin.sh" "${pa_system_test[@]}" "$@"
    else sudo -- bash "$pa_source/scripts/system-plugin.sh" "$@";fi
}
pa_system_state=/usr/local/share/pointeraccel
if ((${#pa_system_test[@]})); then pa_system_state="${pa_system_test[1]}/records";fi
pa_journal=$(python3 "$pa_source/scripts/maintenance.py" journal-path)
if $pa_recover; then
    if [[ -e $pa_system_state/pending && ! -f $pa_journal ]]; then
        pa_error '用户事务日志缺失，已保留系统模块和文件；需要人工核查备份。';exit 1
    fi
    pa_system_call rollback "$pa_uid"
    if [[ -f $pa_journal ]]; then
        pa_recovery=$(python3 "$pa_source/scripts/maintenance.py" journal-action "$pa_system_state/owner.tsv")
        if [[ $pa_recovery == keep ]]; then pa_verify_manifest;fi
        python3 "$pa_source/scripts/maintenance.py" recover-user "$pa_recovery"
    fi
    if ((${#pa_system_test[@]} == 0)); then systemctl --user daemon-reload || true;fi
    printf '安装事务恢复完成。请重新运行 install.sh 检查、构建和安装。\n';exit 0
fi
[[ ! -e $pa_journal ]] || { pa_error '上次安装被中断，请先运行 ./install.sh --recover。';exit 1; }
[[ ! -e $pa_system_state/pending ]] || { pa_error '系统模块事务未结束，请先运行 ./install.sh --recover 核查恢复。';exit 1; }
for pa_tool in cmake ninja g++ pkg-config python3; do command -v "$pa_tool" >/dev/null || { pa_error "缺少依赖: $pa_tool";exit 1; };done
if [[ -e $pa_manifest ]]; then pa_verify_manifest
else
    for pa_path in "$pa_data" "$pa_state"; do [[ ! -e $pa_path ]] || { pa_error "目录已存在且没有安装清单: $pa_path";exit 1; };done
    [[ ! -e $pa_cli && ! -L $pa_cli ]] || { pa_error "CLI 路径已被占用: $pa_cli";exit 1; }
fi
for pa_path in "$pa_unit" "$pa_unit_link"; do
    if [[ -e $pa_path || -L $pa_path ]]; then
        [[ -f $pa_manifest ]] && awk -F '\t' -v path="$pa_path" '$3==path { found=1 } END { exit !found }' "$pa_manifest" || { pa_error "服务路径已被占用: $pa_path";exit 1; }
    fi
done
pa_preflight=$(mktemp -d)
trap 'rm -rf -- "$pa_preflight"' EXIT
python3 "$pa_source/scripts/maintenance.py" fingerprint "$pa_preflight/before.json"
if [[ ${POINTERACCEL_TEST_NO_BUILD:-0} != 1 || ${#pa_system_test[@]} == 0 ]]; then
    cmake -S "$pa_source" -B "$pa_build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
    cmake --build "$pa_build" --parallel "${POINTERACCEL_BUILD_JOBS:-4}"
    ctest --test-dir "$pa_build" --output-on-failure
fi
python3 "$pa_source/scripts/maintenance.py" verify "$pa_preflight/before.json"
pa_program="$pa_build/pointeraccel"
if ((${#pa_system_test[@]})); then pa_program="$pa_build/pointeraccel-test";fi
pa_module="$pa_build/stage/qt6/plugins/kwin/plugins/pointeraccel_kwin.so"
"$pa_program" info --json > "$pa_preflight/info.json"
python3 "$pa_source/scripts/maintenance.py" manifest "$pa_preflight/info.json" "$pa_module" "$pa_preflight/compatibility.json" "$pa_uid"
python3 "$pa_source/scripts/maintenance.py" snapshot "$pa_source" "$pa_preflight/source.tar.gz"
pa_recovery_new_dirs=();pa_parent=${pa_state%/*}
while [[ ! -d $pa_parent ]]; do pa_recovery_new_dirs+=("$pa_parent");pa_parent=${pa_parent%/*};done
mkdir -p -- "${pa_state%/*}"
pa_backup=$(mktemp -d "$pa_state.backup.XXXXXX")
pa_mutated=false;pa_system_changed=false;pa_success=false;pa_new_dirs=("${pa_recovery_new_dirs[@]}")
pa_names=(data config state cli kwin unit unit-link)
pa_paths=("$pa_data" "$pa_config" "$pa_state" "$pa_cli" "$pa_kwin" "$pa_unit" "$pa_unit_link")
for pa_i in "${!pa_paths[@]}"; do
    if [[ -e ${pa_paths[$pa_i]} || -L ${pa_paths[$pa_i]} ]]; then cp -a -- "${pa_paths[$pa_i]}" "$pa_backup/${pa_names[$pa_i]}";fi
done
pa_finish() {
    local result=$?
    if ! $pa_success && $pa_system_changed && [[ ! -e $pa_system_state/pending && -f $pa_journal ]]; then
        local recovery
        recovery=$(python3 "$pa_source/scripts/maintenance.py" journal-action "$pa_system_state/owner.tsv") || recovery=unknown
        if [[ $recovery == keep ]] && pa_verify_manifest; then pa_success=true;fi
    fi
    if ! $pa_success; then
        if $pa_system_changed; then
            if ! pa_system_call rollback "$pa_uid"; then
                pa_error '系统回滚未完成，已保留备份；请运行 ./install.sh --recover。'
                rm -rf -- "$pa_preflight";return 1
            fi
        fi
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
    rm -rf -- "$pa_backup" "$pa_preflight"
    rm -f -- "$pa_journal"
    if ! $pa_success; then for pa_path in "${pa_recovery_new_dirs[@]}"; do rmdir -- "$pa_path" 2>/dev/null || true;done;fi
    return "$result"
}
trap pa_finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for pa_path in "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_config" "$pa_state/backups" "${pa_cli%/*}" "${pa_unit%/*}" "${pa_unit_link%/*}"; do
    pa_parent=$pa_path
    while [[ ! -d $pa_parent ]]; do pa_new_dirs+=("$pa_parent");pa_parent=${pa_parent%/*};done
done
python3 "$pa_source/scripts/maintenance.py" journal-create "$pa_backup" "$pa_system_state/owner.tsv" "$pa_preflight/compatibility.json" "${pa_new_dirs[@]}"
pa_mutated=true
mkdir -p -- "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_config" "$pa_state/backups" "${pa_cli%/*}" "${pa_unit%/*}" "${pa_unit_link%/*}"
chmod 0700 "$pa_state" "$pa_state/backups"
if [[ ! -f "$pa_state/backups/kwin.json" ]]; then
    "$pa_build/pointeraccel" internal-plugin backup "$pa_state/backups/kwin.json"
    if [[ -f "$pa_config/config.json" ]]; then cp -p -- "$pa_config/config.json" "$pa_state/backups/config.before";fi
fi
pa_install_binary() {
    local temporary
    temporary=$(mktemp "$pa_data/bin/.install.XXXXXX")
    install -m0755 -- "$1" "$temporary"
    mv -f -- "$temporary" "$2"
}
pa_install_binary "$pa_program" "$pa_data/bin/pointeraccel"
pa_install_binary "$pa_build/pointeraccel-gui" "$pa_data/bin/pointeraccel-gui"
pa_install_binary "$pa_build/pointeraccel-raw-wayland" "$pa_data/bin/pointeraccel-raw-wayland"
pa_install_binary "$pa_build/pointeraccel-raw-x11" "$pa_data/bin/pointeraccel-raw-x11"
install -m0755 -- "$pa_source/uninstall.sh" "$pa_data/uninstall.sh"
install -m0644 -- "$pa_source/scripts/paths.sh" "$pa_data/scripts/paths.sh"
install -m0755 -- "$pa_source/scripts/system-plugin.sh" "$pa_data/scripts/system-plugin.sh"
install -m0644 -- "$pa_source/scripts/maintenance.py" "$pa_data/scripts/maintenance.py"
install -m0644 -- "$pa_preflight/source.tar.gz" "$pa_data/source.tar.gz"
python3 "$pa_source/scripts/maintenance.py" unit "$pa_unit" "$pa_data/bin/pointeraccel" "${pa_config%/*}" "${pa_data%/*}" "${pa_state%/*}"
ln -sfn -- "$pa_unit" "$pa_unit_link"
install -m0644 -- "$pa_source/README.md" "$pa_data/README.md"
install -m0644 -- "$pa_source/LICENSE" "$pa_data/licenses/PointerAccel.txt"
install -m0644 -- "$pa_source/LICENSES/windows-pointer-linux.txt" "$pa_data/licenses/windows-pointer-linux.txt"
"$pa_data/bin/pointeraccel" internal-init-config
ln -sfn -- "$pa_data/bin/pointeraccel" "$pa_cli"
printf '%s\n' "$pa_uid" > "$pa_state/owner"
pa_sha=$(sha256sum -- "$pa_module");pa_sha=${pa_sha%% *}
pa_system_changed=true
pa_system_call install "$pa_uid" "$pa_module" "$pa_sha" "$pa_preflight/compatibility.json"
if ((${#pa_system_test[@]})) && [[ -n ${POINTERACCEL_TEST_PAUSE_AFTER_MODULE:-} ]]; then
    : > "$POINTERACCEL_TEST_PAUSE_AFTER_MODULE"
    while [[ -e $POINTERACCEL_TEST_PAUSE_AFTER_MODULE ]]; do sleep 0.1;done
fi
if ((${#pa_system_test[@]})) && [[ ${POINTERACCEL_TEST_FAIL_AFTER_MODULE:-0} == 1 ]]; then false;fi
printf '%s\n' "$pa_sha" > "$pa_state/system.sha256"
"$pa_data/bin/pointeraccel" internal-plugin guard > "$pa_state/autostart.sha256"
"$pa_data/bin/pointeraccel" internal-session-stamp "$pa_state/session.json"
pa_manifest_new="$pa_state/manifest.new"
: > "$pa_manifest_new"
for pa_path in "$pa_data/bin/pointeraccel" "$pa_data/bin/pointeraccel-gui" "$pa_data/bin/pointeraccel-raw-wayland" "$pa_data/bin/pointeraccel-raw-x11" "$pa_data/uninstall.sh" "$pa_data/scripts/paths.sh" "$pa_data/scripts/system-plugin.sh" "$pa_data/scripts/maintenance.py" "$pa_data/source.tar.gz" "$pa_data/README.md" "$pa_data/licenses/PointerAccel.txt" "$pa_data/licenses/windows-pointer-linux.txt" "$pa_state/owner" "$pa_state/system.sha256" "$pa_state/session.json" "$pa_unit" "$pa_state/backups/kwin.json" "$pa_state/backups/config.before"; do
    if [[ -f $pa_path ]]; then pa_sha=$(sha256sum -- "$pa_path");printf 'F\t%s\t%s\n' "${pa_sha%% *}" "$pa_path" >> "$pa_manifest_new";fi
done
printf 'L\t%s\t%s\n' "$pa_data/bin/pointeraccel" "$pa_cli" >> "$pa_manifest_new"
printf 'L\t%s\t%s\n' "$pa_unit" "$pa_unit_link" >> "$pa_manifest_new"
if [[ -f "$pa_backup/state/manifest.tsv" ]]; then
    while IFS=$'\t' read -r pa_kind pa_value pa_path; do [[ $pa_kind != D ]] || printf 'D\t-\t%s\n' "$pa_path" >> "$pa_manifest_new";done < "$pa_backup/state/manifest.tsv"
fi
for pa_path in "${pa_new_dirs[@]}"; do printf 'D\t-\t%s\n' "$pa_path" >> "$pa_manifest_new";done
mv -- "$pa_manifest_new" "$pa_manifest"
pa_verify_manifest
if ((${#pa_system_test[@]})) && [[ ${POINTERACCEL_TEST_FAIL_BEFORE_COMMIT:-0} == 1 ]]; then false;fi
pa_system_call commit "$pa_uid"
if ((${#pa_system_test[@]})) && [[ -n ${POINTERACCEL_TEST_PAUSE_AFTER_COMMIT:-} ]]; then
    : > "$POINTERACCEL_TEST_PAUSE_AFTER_COMMIT"
    while [[ -e $POINTERACCEL_TEST_PAUSE_AFTER_COMMIT ]]; do sleep 0.1;done
fi
pa_success=true
if ((${#pa_system_test[@]} == 0)); then
    systemctl --user daemon-reload || pa_error '用户服务重载失败；下次登录仍会重新读取服务。'
    systemctl --user restart pointeraccel-session.service || pa_error '检查服务启动失败，请运行 pointeraccel doctor 查看诊断。'
fi
printf '\n安装完成。请保存工作并手动重新登录 Plasma。当前桌面不会被重启。\nCLI: %s\n卸载: %s/uninstall.sh\n' "$pa_cli" "$pa_data"
