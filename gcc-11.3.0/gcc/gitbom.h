#ifndef GCC_GITBOM_H
#define GCC_GITBOM_H

#include <string>

/* An example implementation for ELF targets.  Defined in varasm.c  */
extern void elf_record_gitbom_write_gitoid (std::string, std::string);

#endif /* ! GCC_GITBOM_H */
