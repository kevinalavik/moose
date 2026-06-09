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
	printf '#ifndef GENERATED_CONFIG_H\n'
	printf '#define GENERATED_CONFIG_H\n\n'
	for name in CONFIG_DRIVER_I8042_KBD; do
		value=$(config_value "$name" y)
		if [ "$value" = y ]; then
			printf '#define %s 1\n' "$name"
		else
			printf '#define %s 0\n' "$name"
		fi
	done
	printf '\n#endif /* GENERATED_CONFIG_H */\n'
} > "$tmp"
mv "$tmp" "$out"
