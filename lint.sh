#!/bin/bash
# If no argument is provided, use the default file
FILE_TO_LINT=${1:-src/momentum_mouse.c}

# Check if we're linting a GUI file
if [[ "$FILE_TO_LINT" == gui/* ]]; then
    # Use the GUI makefile for linting
    cd gui
    make -n | grep gcc | head -1 | sed 's/^/LINT: /' > /dev/null
    # Just check syntax without building
    BASENAME=$(basename "$FILE_TO_LINT")
    if pkg-config --exists gtk+-3.0; then
        GTK_FLAGS=$(pkg-config --cflags gtk+-3.0 glib-2.0)
        gcc -Wall -Wextra -Werror -pedantic -fsyntax-only $GTK_FLAGS -DGTK_AVAILABLE "$BASENAME"
    else
        # If GTK is not available, still check syntax but define GTK_NOT_AVAILABLE
        gcc -Wall -Wextra -Werror -pedantic -fsyntax-only -DGTK_NOT_AVAILABLE "$BASENAME"
    fi
else
    # Use gcc for static analysis for non-GUI files
    gcc -Wall -Wextra -Werror -pedantic -fsyntax-only -I./include "$FILE_TO_LINT"
fi

# Store the return code
RESULT=$?

# If gcc succeeds, print a success message
if [ $RESULT -eq 0 ]; then
    echo "No errors found in $FILE_TO_LINT"
else
    echo "Errors found in $FILE_TO_LINT"
fi

# Return the gcc exit code
exit $RESULT
