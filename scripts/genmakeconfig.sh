#!/bin/sh
set -eu

root=$1
out=$2
tmp="$out.tmp"

config_value() {
	name=$1
	default=$2
	if [ -f "$root/.config" ]; then
		if grep -q "^$name=y$" "$root/.config"; then
			printf y
			return
		fi
		if grep -q "^# $name is not set$" "$root/.config"; then
			printf n
			return
		fi
	fi
	printf '%s' "$default"
}

mkdir -p "$(dirname "$out")"
{
	for name in CONFIG_DRIVER_I8042_KBD; do
		printf '%s := %s\n' "$name" "$(config_value "$name" y)"
	done
} > "$tmp"
mv "$tmp" "$out"
