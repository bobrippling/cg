#ifndef PARSE_H
#define PARSE_H

#include "unit.h"

unit *parse_code(tokeniser *tok, int *const err, const struct target *target);

#endif
