#include <stdlib.h>
#include <string.h>

#include "types.h"

struct string *string_alloc(const char *data, size_t length)
{
	struct string *result = malloc(sizeof(struct string) + sizeof(char) * (length + 1));
	if (!result) return 0;
	result->data = (char *)(result + 1);
	result->length = length;
	if (data) memcpy(result->data, data, length);
	result->data[length] = 0;
	return result;
}
