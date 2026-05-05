/*
 * utils.c -- Misc utility routines.
 */
/*
 * Imported verbatim from walkco/stadia-vigem (master, 2024-04-02 onwards),
 * itself derived from grayver/Mi-ViGEm. Both are MIT-licensed.
 *   https://github.com/walkco/stadia-vigem
 *   https://github.com/grayver/Mi-ViGEm
 * Reused unmodified in this project. Do not reformat — diff cleanliness
 * matters for upstream attribution.
 */

#include <ctype.h>

#include "utils.h"

PTCHAR _tcsistr(PTCHAR haystack, const PTCHAR needle)
{
    do
    {
        PTCHAR h = haystack;
        PTCHAR n = needle;
        while (tolower((TBYTE)*h) == tolower((TBYTE)*n) && *n)
        {
            h++;
            n++;
        }
        if (*n == 0)
        {
            return (PTCHAR)haystack;
        }
    } while (*haystack++);
    return NULL;
}
