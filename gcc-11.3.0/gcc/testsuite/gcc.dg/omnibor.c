/* Test whether .note.omnibor section is created when -frecord-omnibor option
   is passed. Note that the OmniBOR Document file is also created in
   objects/gitoid_blob_sha{1, 256}/<dir>/ inside the gcc/testsuite/gcc directory,
   where <dir> consists of the first two hex characters of the gitoid of that
   file. The remaining hex characters of that gitoid are used as the name of
   that OmniBOR Document file.  */

/* { dg-do compile } */
/* { dg-options "-frecord-omnibor" } */

#include "omnibor.h"

int f()
{
  int var = 0;
  return var;
}

/* { dg-final { scan-assembler "\t.section\t.note.omnibor" } } */
/* { dg-final { scan-assembler "\t.string\t\"\\\\b\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\\\\024\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\\\\001\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"OMNIBOR\"" } } */
/* { dg-final { scan-assembler "\t.ascii\t\"*\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\\\\b\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\" \"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\\\\002\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"\"" } } */
/* { dg-final { scan-assembler "\t.string\t\"OMNIBOR\"" } } */
/* { dg-final { scan-assembler "\t.ascii\t\"*\"" } } */
