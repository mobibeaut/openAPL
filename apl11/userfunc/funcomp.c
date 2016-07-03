/* Copyright U S WEST Advanced Technologies, Inc.
 * You may use, copy, modify and sublicense this Software
 * subject to the conditions expressed in the file "License".
 */
#include <stdio.h>
#include <unistd.h>

#include "apl.h"
#include "utility.h"
#include "opt_codes.h"
#include "memory.h"
#include "parser.h"
#include "userfunc.h"
#include "debug.h"

static void reverse(char *s);

/*
 * funcomp - compile functions
 * during run time, a user defined function is invoked and
 * compiled before being executed.
 *
 * It uses a mix of buffered and nonbuf IO.
 *
 */

char *labcpp,*labcpe;
extern char *catcode();

void funcomp(SymTabEntry *np) {
   int linesRead = 0;
   char labp[MAXLAB*20], labe[MAXLAB*4];
   char  *a, *c; 
   int  i, err, err_code;
   char **p;
   char    *iline, *status, *phase, *err_msg;
   struct Context *original_gsip, *FunctionLine, 
                  *Prologue, *Epilogue;
   FILE *infile;

   /* as gsip is used during compilation, we have to save the original
    * and restore it upon exit
    */
   original_gsip=gsip;    
   err_code=0; err_msg="";
   infile = fdopen(wfile,"r");
   err=fseek(infile, (long)np->label, 0);
   if ( err != 0 ) error(ERR_implicit,"Could not find function in workspace");
   err = 0;
   lineNumber = 0;
   iline=(char *)alloc(LINEMAX);

  /* Phase 1 creates the first of a linked list of compiled
   * function lines.  This first line will contain the prologue
   */
   if (code_trace) {
       fprintf(stderr, "Phase 1 \n");
   }
   phase="Phase 1";
   Prologue=(struct Context *)alloc(sizeof(struct Context));
   Prologue->Mode=deffun;
   Prologue->suspended=0;
   Prologue->prev=0;
   Prologue->text=(char *)NULL;
   Prologue->pcode=(char *)NULL;
   Prologue->sp=0;

   /* get the first line */
   status = readLine("funcomp prolog line", iline, LINEMAX, infile);

   if ( 0 == strlen(iline) || status == NULL) {
      err_code=ERR_implicit;
      err_msg="empty header line";
      goto out;
   } else {
    ++linesRead;
   }

   Prologue->text=iline;
   gsip=Prologue;
   labgen = 0;
   compile_new(3);    /* 3 = compile function prologue */
   if(gsip->pcode == 0) {
      err_code=ERR_implicit;
      err_msg="invalid header line";
      goto out;
   }

  /* Phase 2 compiles the body of the function */
   if (code_trace) {
       fprintf(stderr, "Phase 2 \n");
   }
   phase="Phase 2";
   labcpp = labp;
   labcpe = labe;
   labgen = 1;

   while (1) {
      status = readLine("funcomp function body", iline,LINEMAX,infile);
      if ( 0 == strlen(iline) || status == NULL) {
          break;
      } else {
        ++linesRead;
      }

      /* create a new Context */
      FunctionLine=(struct Context *)alloc(sizeof(struct Context));
      FunctionLine->Mode=deffun;
      FunctionLine->suspended=0;
      FunctionLine->prev=gsip;    /* link to previous */
      FunctionLine->text=(char *)NULL;
      FunctionLine->pcode=(char *)NULL;
      FunctionLine->sp=0;
      FunctionLine->text=iline;

      gsip=FunctionLine;
      lineNumber++;
      compile_new(5);    /* 5 = compile function body */
      if ( MAXLAB <= (labcpe-labe)/5+1) {
         err_code=ERR_botch;
         err_msg="too many labels, edit MAXLAB in apl.h and recompile";
         goto out;
      }
      if(gsip->pcode == 0) {
         err++;
      } 
   }

   if (err) {
      err_code=ERR_implicit;
      err_msg="compilation errors";
      goto out;
   }

   /* At the end of this Phase, lineNumber=Maximum_No_of_lines
    * but we want to include the Prologue (line 0) and 
    * Epilogue (so add one to lineNumber)
    */
    lineNumber++;

   if (code_trace) {
       fprintf(stderr, "At end of Phase 2...\n");
       for (i=lineNumber; i>1; i-- ) {
          fprintf(stderr, "[%d] ",i-1);
          code_dump(FunctionLine->pcode,0);
          FunctionLine=FunctionLine->prev;
       }
       fprintf(stderr, "[p] "); code_dump(Prologue->pcode,0);
       fprintf(stderr, "[0] %d\n",lineNumber);
   }
   
   /* Phase 3 - dealing with labels */
   phase="Phase 3a";

   /* generate the Epilogue */

   // reset the file read pointer to the beginning of this function..
   fseek(infile, (long)np->label, 0);

   // we are rereading the first line of the function, so don't bump lineNumber.
   status = readLine("funcomp epilog after rewinding to label",
                     iline, LINEMAX, infile);

   if (0 == strlen(iline)) {
      err++;
      err_code=ERR_implicit;
      goto out;
   }

   Epilogue=(struct Context *)alloc(sizeof(struct Context));
   Epilogue->Mode=deffun;
   Epilogue->suspended=0;
   Epilogue->prev=gsip;
   Epilogue->text=iline;
   Epilogue->pcode=(char *)NULL;
   Epilogue->sp=0;
   labgen = 0;
   gsip=Epilogue;
   compile_new(4);    /* 4 = compile function epilogue */
   if(gsip->pcode == 0) {
      err_code=ERR_implicit;
      err_msg="invalid header line";
      goto out;
   }

   /* only conduct phase 3b/c if labels were generated */
   if(labcpp != labp){
      phase="Phase 3b";
      /* append the label-epilogue to the Epilogue */
      reverse(labe);
      c=Epilogue->pcode;
      Epilogue->pcode = catcode(labe, c);
      aplfree((int *) c);

      phase="Phase 3c";
      /* At this point, we have:
       * fn-prologue (p[1]):      <AUTOs and ARGs>, ELID, END
       * label-prologue (labp):   <AUTOs and LABELs>, END
       * 
       * and we want to produce:
       * fn-prologue (p[1]):   <AUTOs and ARGs>,<AUTOs and LABELs>,  ELID, END.
       */
      a = csize(Prologue->pcode) - 1;
      c = csize(labp) - 1;

      /* Move the ELID from the end of the fn-prologue,
       * to the end of the label-prologue.
       */
      if (((struct chrstrct *)Prologue->pcode)->c[(int)a-1] == ELID) {
         ((struct chrstrct *)Prologue->pcode)->c[(int)a-1] = END;
         labp[(int)c] = ELID;
         labp[(int)c+1] = END;
      } else {
         err_code=ERR_botch;
         err_msg="elid";
         goto out;
      }

      /* Append the label-prologue to the Prologue */
      a = Prologue->pcode;
      Prologue->pcode = catcode(a,labp);
      aplfree((int *) a);
   }

   if(code_trace) {
      code_dump(Prologue->pcode, 1);    /* show the prologue */
      code_dump(Epilogue->pcode, 1);    /* show the epilogue */
   }

  /* Phase 4 goes through the compiled lines
   * storing pointers to each pcode in p[]
   */
   if (code_trace) {
       fprintf(stderr, "Phase 4 \n");
   }
   phase="Phase 4";
   p = (char **)alloc((linesRead + 1) * SPTR);
   FunctionLine = Epilogue;
   for (i = linesRead; i >= 0; --i) {
      p[i] = FunctionLine->pcode;
      FunctionLine=FunctionLine->prev;
   }

   if (code_trace) {
       fprintf(stderr, "At end of Phase 4...\n");
       fprintf(stderr, "[p] "); code_dump(p[0],0);
       for (i=1; i<linesRead; i++ ) {
          fprintf(stderr, "[%d] ",i);
          code_dump(p[i],0);
       }
       fprintf(stderr, "[e] "); code_dump(p[linesRead],0);
   }

   /* put the result into effect */
   // np->itemp = (struct item *)p;

   // functionLineCount is one larger than the APL function line number
   // of the last line of this function.  i.e., if gsip->funlc >= functionLineCount
   // or gsip->funlc <= 0, the function is done and should exit.
   // if the function body goes from [1] to [N], we will have read N+1 lines
   // counting the function header.  So, N+1 is the number we are looking for.

   np->functionLineCount = linesRead;
   np->functionPcodeLines = p;
   np->functionPcodeLineLength = linesRead + 1;

   err = 0;

out:
   if (code_trace) {
       fprintf(stderr, "Phase out \n");
   }

   fclose(infile);
   aplfree((int *) iline);
   gsip=original_gsip;    
   if (err_code) {
      if (np->namep) printf("%s in function %s\n", phase, np->namep);
      error(err_code,err_msg);
   }
}

static void reverse(char *s) {
   char *p, *q, c;
   int j;

   p = q = s;
   while(*p != END) p++;
   p -= 1+sizeof(char *);
   while(q < p){
      for(j=0; j<1+sizeof (char *); j++) {
         c = p[j];
         p[j] = q[j];
         q[j] = c;
      }
      q += j;
      p -= j;
   }
}
