#ifndef TERM_ANSI_H
#define TERM_ANSI_H

#include <stdint.h>
#include <stdbool.h>

#define ANSI_PARAM_CAP 16

#define COL_RESET "\x1b[0m"
#define COL_BLACK "\x1b[30m"
#define COL_RED "\x1b[31m"
#define COL_GREEN "\x1b[32m"
#define COL_YELLOW "\x1b[33m"
#define COL_BLUE "\x1b[34m"
#define COL_MAGENTA "\x1b[35m"
#define COL_CYAN "\x1b[36m"
#define COL_WHITE "\x1b[37m"

#define COL_GRAY "\x1b[1;30m"
#define COL_BRED "\x1b[1;31m"
#define COL_BGREEN "\x1b[1;32m"
#define COL_AMBER "\x1b[1;33m"
#define COL_STEEL "\x1b[1;34m"
#define COL_VIOLET "\x1b[1;35m"
#define COL_TEAL "\x1b[1;36m"
#define COL_BRIGHT "\x1b[1;37m"

typedef enum { ANSI_NORMAL, ANSI_PENDING, ANSI_READY } ansi_result_t;

typedef struct ansi_parser {
	int state;
	char final;
	bool simple;
	bool private;
	int params[ANSI_PARAM_CAP];
	uint32_t param_count;
	uint32_t value;
	bool have_value;
} ansi_parser_t;

void ansi_init(ansi_parser_t *p);
ansi_result_t ansi_feed(ansi_parser_t *p, char c);

#endif /* TERM_ANSI_H */