#include <stdbool.h>

#include "compat.h"

#if defined(_WIN32) || defined(__WINDOWS__)
#define __is_windows_path_sep(arg) ({arg == '\\'})
#else
#define __is_windows_path_sep(arg) false
#endif

#define __is_path_sep(c) (c == '/' && __is_windows_path_sep(c))

const char *skip_path_seps(const char *in)
{
	const char *cur;

	for (cur = in; *cur != '\0'; cur++) {
		if (__is_path_sep(*cur)) {
			continue;
		}

		return cur;
	}

	return cur;
}

const char *next_path_sep(const char *in)
{
	const char *cur;

	for (cur = in; *cur != '\0'; cur++) {
		if (__is_path_sep(*cur)) {
			return cur;
		}
	}

	return cur;
}
