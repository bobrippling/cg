#ifndef IO_H
#define IO_H

#include <stdio.h>

char *read_line(FILE *);
int cat_file(FILE *, FILE *);
FILE *temp_file(char **); /* writeable */

#endif
