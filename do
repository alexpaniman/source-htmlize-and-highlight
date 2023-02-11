#!/bin/bash

parent_path=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )

$parent_path/generate-html "$1" "$1.tmp.html"
cat "$2" | $parent_path/out "$1.tmp.html" > "$(basename "$1.html")"
rm "$1.tmp.html"
