#ifndef GCC_OMNIBOR_H
#define GCC_OMNIBOR_H

#include <string>

/* An example implementation for ELF targets.  Defined in varasm.c  */
extern void elf_record_omnibor_write_gitoid (std::string, std::string);

#endif /* ! GCC_OMNIBOR_H */
