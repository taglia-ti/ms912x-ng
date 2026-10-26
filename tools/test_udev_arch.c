// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>

/* Supply the architecture input to udev's real rule engine in the fixture. */
int uname_architecture(void)
{
	const char *architecture = getenv("MS912X_TEST_ARCH");
	int (*from_string)(const char *) = dlsym(RTLD_NEXT, "architecture_from_string");
	int (*real_architecture)(void) = dlsym(RTLD_NEXT, "uname_architecture");

	if (architecture && from_string)
		return from_string(architecture);
	return real_architecture();
}
