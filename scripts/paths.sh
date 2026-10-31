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

pa_allowed_file() {
	[[ $1 != *'/../'* && $1 != *'/./'* ]] || return 1
	case "$1" in
	"$pa_data"/* | "$pa_state"/backups/* | "$pa_state"/owner | "$pa_state"/system.sha256 | "$pa_state"/session.json | "$pa_unit")
		return 0
		;;
	esac
	return 1
}

pa_allowed_directory() {
	local root
	[[ $1 != / && $1 != "$HOME" && $1 != *'/../'* ]] || return 1
	for root in "$pa_data" "$pa_config" "$pa_state" "${pa_cli%/*}" "${pa_unit%/*}" "${pa_unit_link%/*}"; do
		[[ $root == "$1" || $root == "$1/"* ]] && return 0
	done
	[[ $1 == "$pa_data/"* || $1 == "$pa_state/"* ]]
}

pa_verify_manifest() {
	[[ -f $pa_manifest && ! -L $pa_manifest && $(cat "$pa_state/owner") == "$pa_uid" ]] || {
		pa_error '缺少有效的当前用户安装清单。'
		return 1
	}
	local kind value path actual
	for path in "$pa_data/bin" "$pa_data/scripts" "$pa_data/licenses" "$pa_state/backups"; do
		[[ ! -L $path ]] || {
			pa_error "安装子目录变成了符号链接: $path"
			return 1
		}
	done
	while IFS=$'\t' read -r kind value path; do
		case "$kind" in
		F)
			pa_allowed_file "$path" && [[ -f $path && ! -L $path ]] || {
				pa_error "清单路径无效: $path"
				return 1
			}
			actual=$(sha256sum -- "$path")
			actual=${actual%% *}
			[[ $actual == "$value" ]] || {
				pa_error "文件被修改，已停止以避免误删: $path"
				return 1
			}
			;;
		L)
			[[ -L $path && $(readlink -- "$path") == "$value" ]] &&
				{
					[[ $path == "$pa_cli" && $value == "$pa_data/bin/pointeraccel" ]] || [[ $path == "$pa_unit_link" && $value == "$pa_unit" ]]
				} || {
				pa_error '安装入口冲突。'
				return 1
			}
			;;
		D)
			pa_allowed_directory "$path" || {
				pa_error '目录清单超出允许范围。'
				return 1
			}
			;;
		*)
			pa_error '安装清单格式不正确。'
			return 1
			;;
		esac
	done <"$pa_manifest"
}
