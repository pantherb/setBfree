#ifndef FMEMSTREAM_H
#define FMEMSTREAM_H

#ifndef HAVE_MEMSTREAM
FILE *fmemopen(void *buf, size_t size, const char *mode);
FILE * open_memstream (char **buf, size_t *len);
#endif

#endif
