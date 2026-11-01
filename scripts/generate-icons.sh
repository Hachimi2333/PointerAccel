#!/usr/bin/env bash
# Run after editing the SVG; normal builds use the committed PNGs directly.
set -euo pipefail
pa_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
pa_icons="$pa_root/src/gui/icons"
command -v magick >/dev/null || {
	printf 'Generating icons requires ImageMagick with SVG support (magick).\n' >&2
	exit 1
}
for pa_size in 16 24 32 48 64 128 256 512; do
	# Binary outer alpha avoids colored fringes in Qt's Wayland icon buffers.
	magick -background none -density 384 "$pa_icons/pointeraccel.svg" \
		-resize "${pa_size}x${pa_size}" -channel A -threshold 50% +channel \
		-background black -alpha background -strip -define png:exclude-chunk=all \
		"PNG32:$pa_icons/pointeraccel-$pa_size.png"
done
printf 'Generated PointerAccel PNG icons in %s\n' "$pa_icons"
