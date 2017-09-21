#ifndef IO_H
#define IO_H

#include <stdio.h>

char *read_line(FILE *);
char **read_lines(FILE *, size_t *const);
int cat_file(FILE *, FILE *);
FILE *temp_file(char **); /* writeable */

#endif
