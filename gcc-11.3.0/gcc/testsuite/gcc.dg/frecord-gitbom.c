/* Test whether .bom section is created when -frecord-gitbom=<arg> option is passed.
   Note that the GitBOM Document file is also created in <arg>/.gitbom/objects/<dir>/
   inside the gcc/testsuite/gcc directory, where <dir> consists of the first two
   hex characters of the 40-character gitoid of that file. The remaining 38 hex
   characters of that gitoid are used as the name of that GitBOM Document file.  */

/* { dg-do compile } */
/* { dg-options "-frecord-gitbom=gitbomdir" } */

#include "gitbom.h"

int f()
{
  int var = 0;
  return var;
}

/* { dg-final { scan-assembler "\t.section\t.bom" } } */
/* { dg-final { scan-assembler "\t.ascii\t\"*\"" } } */
