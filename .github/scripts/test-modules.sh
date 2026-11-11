#!/bin/sh
# Print the module directories whose test suites CI should run.
#
# usage: test-modules.sh [<base commit>]
#
# Always includes the modules listed in .github/wine-aa-test-modules.
# With a base commit, also includes every dlls/* or programs/* module
# changed between that commit and HEAD. Only modules that have a tests
# directory are printed.

top=$(git rev-parse --show-toplevel) || exit 1
cd "$top" || exit 1

if [ -n "$1" ] && ! git rev-parse --verify -q "$1^{commit}" >/dev/null; then
    echo "test-modules.sh: unknown base commit '$1'" >&2
    exit 1
fi

{
    grep -v '^[[:space:]]*\(#\|$\)' .github/wine-aa-test-modules
    if [ -n "$1" ]; then
        git diff --name-only "$1" HEAD -- dlls programs |
            sed -n 's#^\(\(dlls\|programs\)/[^/]*\)/.*#\1#p'
    fi
} | sort -u | while read -r module; do
    [ -d "$module/tests" ] && echo "$module"
done
