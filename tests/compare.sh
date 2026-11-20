#!/bin/sh
# Ask the core the questions in questions.txt at creativity 0; the answers
# must match expected.txt (recorded from the original 2.1 release).
set -e
cd "$(dirname "$0")/.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT
build/tiny 0 < tests/questions.txt | sed -n 's/^>   //p' > "$out"
paste -d '|' tests/questions.txt "$out" | sed 's/|/  =>  /'
if diff tests/expected.txt "$out"; then
    echo "OK: $(wc -l < "$out" | tr -d ' ') answers as expected"
else
    echo "DIFFERENT answers (< expected, > got)"; exit 1
fi
