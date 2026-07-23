// ----------------------------------------------------------------------------
// CPPComp — diagnostics --------------------------------------------------------
// ----------------------------------------------------------------------------

#ifndef CPPCOMP_MESSAGES_H
#define CPPCOMP_MESSAGES_H

void msg_set_file(const char *name);
_Noreturn void msg_error(int line, const char *fmt, ...); // prints and exits(1)
void msg_warning  (int line, const char *fmt, ...);
_Noreturn void msg_internal(const char *fmt, ...);     // compiler-internal failure
int  msg_error_count(void);

#endif
