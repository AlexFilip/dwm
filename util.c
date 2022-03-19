/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void *ecalloc(size_t count, size_t size) {
	void *p = calloc(count, size);

	if (!p)
		die("calloc:");

	return p;
}

void die(const char *fmt, ...) {
	va_list ap;

    FILE* ErrorOutput = fopen("~/dwm-error.txt", "w");

	va_start(ap, fmt);
	// vfprintf(stderr, fmt, ap);
	vfprintf(ErrorOutput, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		// fputc(' ', stderr);
		fputc(' ', ErrorOutput);
		perror(NULL);
	} else {
		// fputc('\n', stderr);
		fputc('\n', ErrorOutput);
	}

	exit(1);
}
