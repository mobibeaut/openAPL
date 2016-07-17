/* Copyright U S WEST Advanced Technologies, Inc.
 * You may use, copy, modify and sublicense this Software
 * subject to the conditions expressed in the file "License".
 */

#include "apl.h"
#include "utility.h"

data
    ex_gt(d1, d2)
        data d1,
    d2;
{
    if (fuzz(d1, d2) > 0)
        return (one);
    return (zero);
}
