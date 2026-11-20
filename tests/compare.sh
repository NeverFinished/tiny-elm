#!/bin/sh
# Ask the new core and the original DOS program (plain mode, built with the
# host compiler) the same questions at creativity 0; answers must match.
set -e
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp model/TINY.BIN "$tmp/TINY.BIN"
(cd "$tmp" && "$OLDPWD/build/tiny_dos" 0 < "$OLDPWD/tests/questions.txt") |
    sed -n 's/^>   //p' > "$tmp/old.txt"
build/tiny 0 < tests/questions.txt | sed -n 's/^>   //p' > "$tmp/new.txt"
paste -d '|' tests/questions.txt "$tmp/new.txt" | sed 's/|/  =>  /'
if diff "$tmp/old.txt" "$tmp/new.txt" > "$tmp/diff.txt"; then
    echo "OK: $(wc -l < "$tmp/new.txt" | tr -d ' ') answers identical to the original"
else
    echo "DIFFERENT answers:"; cat "$tmp/diff.txt"; exit 1
fi
