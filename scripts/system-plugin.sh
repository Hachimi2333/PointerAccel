#!/usr/bin/env bash
set -euo pipefail
# The only privileged operations: one module and its root-owned transaction record.
pa_test=false
if [[ ${1:-} == --test-root ]]; then
	[[ $(id -u) != 0 ]] || {
		printf '模拟系统目录测试必须以普通用户运行。\n' >&2
		exit 1
	}
	pa_test=true
	pa_test_root=${2:?}
	shift 2
	[[ $pa_test_root == /* && $pa_test_root != / && $pa_test_root != *'/../'* ]] || exit 2
	pa_target="$pa_test_root/plugins/pointeraccel_kwin.so"
	pa_system_state="$pa_test_root/records"
else
	[[ $(id -u) == 0 ]] || {
		printf '系统模块操作需要 sudo。\n' >&2
		exit 1
	}
	pa_target=/usr/lib64/qt6/plugins/kwin/plugins/pointeraccel_kwin.so
	pa_system_state=/usr/local/share/pointeraccel
fi
if $pa_test; then
	pa_lock_key=$(printf '%s' "$pa_test_root" | sha256sum)
	pa_lock_key=${pa_lock_key%% *}
	pa_lock_path="/tmp/pointeraccel-system-test-$(id -u)-$pa_lock_key.lock"
else
	pa_lock_path=/run/pointeraccel.lock
fi
exec {pa_lock_fd}>"$pa_lock_path"
flock --exclusive "$pa_lock_fd"
pa_action=${1:?}
pa_owner=${2:?}
shift 2
[[ $pa_owner =~ ^[0-9]+$ ]] || exit 2
if ! $pa_test && [[ -n ${SUDO_UID:-} && $SUDO_UID != "$pa_owner" ]]; then
	printf '安装用户不匹配。\n' >&2
	exit 1
fi
for pa_item in "$pa_system_state" "$pa_target" "$pa_system_state/compatibility.json"; do
	[[ ! -L $pa_item ]] || {
		printf '系统安装路径存在符号链接。\n' >&2
		exit 1
	}
done
pa_record="$pa_system_state/owner.tsv"
pa_compatibility="$pa_system_state/compatibility.json"
pa_maintenance=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/maintenance.py

pa_hash() {
	local result
	result=$(sha256sum -- "$1")
	printf '%s' "${result%% *}"
}

pa_validate_owner() {
	local owner hash metadata
	[[ -f $pa_record && ! -L $pa_record && -f $pa_target ]] || {
		printf '系统模块清单缺失，拒绝覆盖。\n' >&2
		return 1
	}
	IFS=$'\t' read -r owner hash metadata <"$pa_record"
	[[ $owner == "$pa_owner" && $hash =~ ^[0-9a-f]{64}$ && $(pa_hash "$pa_target") == "$hash" ]] || {
		printf '系统模块的所有者或校验值不匹配。\n' >&2
		return 1
	}
	if [[ -n $metadata ]]; then
		[[ $metadata =~ ^[0-9a-f]{64}$ && -f $pa_compatibility && $(pa_hash "$pa_compatibility") == "$metadata" ]] || {
			printf '系统兼容清单校验失败。\n' >&2
			return 1
		}
	else
		[[ ! -e $pa_compatibility ]] || return 1
	fi
}

pa_rollback() {
	if [[ -n ${pa_temporary:-} ]]; then
		rm -f -- "$pa_temporary"
	fi
	if [[ -n ${pa_manifest_temporary:-} ]]; then
		rm -f -- "$pa_manifest_temporary"
	fi
	[[ -f "$pa_system_state/pending" && $(cat "$pa_system_state/pending") == "$pa_owner" ]] || return 0
	if [[ -f "$pa_system_state/previous.so" ]]; then
		mv -f -- "$pa_system_state/previous.so" "$pa_target"
		mv -f -- "$pa_system_state/previous.owner" "$pa_record"
	else
		rm -f -- "$pa_target" "$pa_record"
	fi
	if [[ -f "$pa_system_state/previous.compatibility" ]]; then
		mv -f -- "$pa_system_state/previous.compatibility" "$pa_compatibility"
	else
		rm -f -- "$pa_compatibility"
	fi
	rm -f -- "$pa_system_state/pending"
	rmdir -- "$pa_system_state" 2>/dev/null || true
}

case "$pa_action" in
install)
	pa_source=${1:?}
	pa_expected=${2:?}
	pa_manifest_source=${3:?}
	[[ $pa_expected =~ ^[0-9a-f]{64}$ && -f $pa_source && $(pa_hash "$pa_source") == "$pa_expected" ]] || exit 1
	python3 "$pa_maintenance" validate "$pa_manifest_source" "$pa_owner" "$pa_source"
	[[ ! -e "$pa_system_state/pending" ]] || {
		printf '上次系统安装事务未结束，请先回滚。\n' >&2
		exit 1
	}
	if [[ -e $pa_target || -e $pa_record ]]; then
		pa_validate_owner
	fi
	mkdir -p -- "$pa_system_state" "${pa_target%/*}"
	if [[ -f $pa_target ]]; then
		cp -a -- "$pa_target" "$pa_system_state/previous.so"
		cp -a -- "$pa_record" "$pa_system_state/previous.owner"
	fi
	if [[ -f $pa_compatibility ]]; then
		cp -a -- "$pa_compatibility" "$pa_system_state/previous.compatibility"
	fi
	printf '%s\n' "$pa_owner" >"$pa_system_state/pending"
	pa_install_done=false
	trap 'if ! $pa_install_done;then pa_rollback;fi' EXIT
	trap 'exit 130' INT
	trap 'exit 143' TERM
	pa_temporary=$(mktemp "${pa_target}.XXXXXX")
	install -m 0644 -- "$pa_source" "$pa_temporary"
	[[ $(pa_hash "$pa_temporary") == "$pa_expected" ]]
	mv -f -- "$pa_temporary" "$pa_target"
	if ! $pa_test; then
		restorecon -F "$pa_target"
	fi
	pa_manifest_temporary=$(mktemp "$pa_system_state/compatibility.XXXXXX")
	install -m0644 -- "$pa_manifest_source" "$pa_manifest_temporary"
	python3 "$pa_maintenance" validate "$pa_manifest_temporary" "$pa_owner" "$pa_target"
	mv -f -- "$pa_manifest_temporary" "$pa_compatibility"
	printf '%s\t%s\t%s\n' "$pa_owner" "$pa_expected" "$(pa_hash "$pa_compatibility")" >"$pa_record"
	chmod 0644 "$pa_record"
	pa_install_done=true
	;;
commit)
	pa_validate_owner
	[[ $(cat "$pa_system_state/pending") == "$pa_owner" ]]
	python3 "$pa_maintenance" validate "$pa_compatibility" "$pa_owner" "$pa_target"
	rm -f -- "$pa_system_state/previous.so" "$pa_system_state/previous.owner" \
		"$pa_system_state/previous.compatibility" "$pa_system_state/pending"
	;;
rollback)
	pa_rollback
	;;
remove)
	pa_expected=${1:?}
	pa_validate_owner
	[[ ! -e "$pa_system_state/pending" && $(pa_hash "$pa_target") == "$pa_expected" ]]
	rm -- "$pa_target" "$pa_record"
	rm -f -- "$pa_compatibility"
	rmdir -- "$pa_system_state" 2>/dev/null || true
	if $pa_test; then
		rmdir -- "${pa_target%/*}" 2>/dev/null || true
	fi
	;;
*)
	exit 2
	;;
esac
