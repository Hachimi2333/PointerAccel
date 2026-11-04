#!/usr/bin/env bash
# Shared installation paths. Never source installation manifests as shell code.
pa_uid=$(id -u)
pa_data="${XDG_DATA_HOME:-$HOME/.local/share}/pointeraccel"
pa_config="${XDG_CONFIG_HOME:-$HOME/.config}/pointeraccel"
pa_state="${XDG_STATE_HOME:-$HOME/.local/state}/pointeraccel"
pa_cli="$HOME/.local/bin/pointeraccel"
pa_kwin="${XDG_CONFIG_HOME:-$HOME/.config}/kwinrc"
pa_manifest="$pa_state/manifest.tsv"
pa_unit="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/pointeraccel-session.service"
pa_unit_link="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service"

pa_error() {
	printf 'PointerAccel: %s\n' "$*" >&2
}

pa_check_paths() {
	[[ $pa_uid != 0 ]] || {
		pa_error '请用普通用户运行，脚本仅在安装独立系统模块时调用 sudo。'
		return 1
	}
	local path
	for path in "$pa_data" "$pa_config" "$pa_state" "$pa_cli" "$pa_kwin" "$pa_unit" "$pa_unit_link"; do
		[[ $path == /* && $path != *$'\n'* && $path != *$'\t'* && $path != *'/../'* && $path != *'/./'* ]] || {
			pa_error "不支持的路径: $path"
			return 1
		}
	done
	for path in "$pa_data" "$pa_config" "$pa_state"; do
		[[ ! -L $path ]] || {
			pa_error "安装目录不能是符号链接: $path"
			return 1
		}
	done
	[[ ! -L $pa_kwin ]] || {
		pa_error 'kwinrc 是符号链接，需先协商配置恢复方式。'
		return 1
	}
	[[ ! -L $pa_config/config.json ]] || {
		pa_error '程序配置不能是符号链接。'
		return 1
	}
	for path in "${pa_unit%/*}" "${pa_unit_link%/*}"; do
		[[ ! -L $path ]] || {
			pa_error "服务目录不能是符号链接: $path"
			return 1
		}
	done
}

pa_verify_manifest() {
	python3 "$(dirname -- "${BASH_SOURCE[0]}")/maintenance.py" verify-installation
}
