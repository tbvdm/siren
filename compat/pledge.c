/*
 * Written by Tim van der Molen.
 * Public domain.
 */

#include "../config.h"
#include "../compat.h"

int
pledge(UNUSED const char *promises, UNUSED const char *execpromises)
{
	return 0;
}
