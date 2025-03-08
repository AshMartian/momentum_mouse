#!/bin/bash
# If no argument is provided, use the default file
FILE_TO_LINT=${1:-src/inertia_scroller.c}

# Use gcc for static analysis instead of splint
gcc -Wall -Wextra -Werror -pedantic -fsyntax-only -I./include "$FILE_TO_LINT"

# If gcc succeeds, print a success message
if [ $? -eq 0 ]; then
    echo "No errors found in $FILE_TO_LINT"
fi
