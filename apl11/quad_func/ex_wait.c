/* Copyright U S WEST Advanced Technologies, Inc.
 * You may use, copy, modify and sublicense this Software
 * subject to the conditions expressed in the file "License".
 */
#include <signal.h>

#include "apl.h"
#include "utility.h"

ex_wait()
{
   struct item *p;
   int (*sig)(), pid;
   int s;

   SECURITY_CHECK;
   sig = signal(SIGINT, SIG_IGN);
   pid = wait(&s);
   signal(SIGINT, sig);
   p = newdat(DA, 1, 3);
   p->datap[0] = pid;
   p->datap[1] = s&0377;
   p->datap[2] = (s>>8)&0377;
   pop();      /* dummy arg */
   *sp++ = p;
}

