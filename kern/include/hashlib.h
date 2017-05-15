#ifndef __HASHLIB_H__
#define __HASHLIB_H__

#include<types.h>

uint32_t calculate_hash(const unsigned char *ptr, int len, int mod);
#endif
