#ifndef mymalloch
#define mymalloch

#include <stdio.h>
#include <stdlib.h>

#define malloc(x) mymalloc( x, __FILE__, __LINE__);
#define free(x) myfree(x, __FILE__, __LINE__);

void* mymalloc(size_t size, const char* file, int line);
void myfree(void* ptr, const char* file, int line);

#endif
