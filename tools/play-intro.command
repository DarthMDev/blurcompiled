#!/bin/sh
# Native resource viewer only; this does not launch the translated game.
set -eu
viewer_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
game_data_root=${1:-/Volumes/T7-Touch/Games/Blur}
movie_name=${2:-atvi.bik}
exec "$viewer_root/build/blur-movie" --assets "$game_data_root" --movie "$movie_name"
