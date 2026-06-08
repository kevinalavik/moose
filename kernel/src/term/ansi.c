#include <term/ansi.h>

enum { ANSI_NORM, ANSI_ESC, ANSI_CSI };

void ansi_init(ansi_parser_t *p)
{
	p->state = ANSI_NORM;
	p->final = 0;
	p->simple = false;
	p->private = false;
	p->param_count = 0;
	p->value = 0;
	p->have_value = false;
}

static void push_param(ansi_parser_t *p)
{
	if (p->param_count < ANSI_PARAM_CAP) {
		if (p->have_value)
			p->params[p->param_count++] = (int)p->value;
		else
			p->params[p->param_count++] = -1;
	}
	p->value = 0;
	p->have_value = false;
}

ansi_result_t ansi_feed(ansi_parser_t *p, char c)
{
	switch (p->state) {
	case ANSI_NORM:
		if ((unsigned char)c == 0x1b) {
			p->state = ANSI_ESC;
			return ANSI_PENDING;
		}
		return ANSI_NORMAL;

	case ANSI_ESC:
		if (c == '[') {
			p->state = ANSI_CSI;
			p->param_count = 0;
			p->value = 0;
			p->have_value = false;
			p->private = false;
			return ANSI_PENDING;
		}

		p->simple = true;
		p->final = c;
		p->state = ANSI_NORM;

		switch (c) {
		case '7':
		case '8':
		case 'D':
		case 'E':
		case 'c':
			return ANSI_READY;
		}

		return ANSI_READY;

	case ANSI_CSI:
		p->simple = false;

		if (c == '?' && p->param_count == 0 && !p->have_value) {
			p->private = true;
			return ANSI_PENDING;
		}

		if (c >= '0' && c <= '9') {
			p->have_value = true;
			p->value = p->value * 10 + (uint32_t)(c - '0');
			return ANSI_PENDING;
		}

		if (c == ';' || c == ':') {
			push_param(p);
			return ANSI_PENDING;
		}

		if (c >= 0x40 && c <= 0x7e) {
			push_param(p);
			p->final = c;
			p->state = ANSI_NORM;
			return ANSI_READY;
		}

		ansi_init(p);
		return ANSI_PENDING;
	}

	return ANSI_NORMAL;
}
