#!/bin/sh
cd "$(dirname "$(realpath "$0")")"
exec python3 -m dmctl "$@"
