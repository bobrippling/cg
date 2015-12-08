#ifndef LBL_H
#define LBL_H

#include <stdbool.h>

char *lbl_new_private(unsigned *const counter, const char *prefix);
char *lbl_new_ident(const char *ident, const char *prefix);
bool lbl_equal_to_ident(const char *lbl, const char *ident, const char *prefix);

#endif
