#include "string.h"

void kmemset(void *p, uint8_t v, size_t n) {
    uint8_t *b = p; while (n--) *b++ = v;
}
void kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src; while (n--) *d++ = *s++;
}
size_t kstrlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int kstrncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n == (size_t)-1 ? 0 : (unsigned char)*a - (unsigned char)*b;
}
char *kstrcpy(char *dst, const char *src) {
    char *r = dst; while ((*dst++ = *src++)); return r;
}
char *kstrncpy(char *dst, const char *src, size_t n) {
    /* Copy at most n bytes from src, then NUL-pad the remainder of dst.
     * The previous "while (n--)" form wrapped n to SIZE_MAX whenever src
     * held no NUL within the first n bytes (e.g. a 1-char ext2 name),
     * zero-filling memory almost forever past the destination. */
    char *r = dst;
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i++] = '\0';
    }
    return r;
}

char *kstrncat(char *dst, const char *src, size_t n) {
    char *r = dst;
    while (*dst) dst++;
    while (n-- && (*dst++ = *src++));
    *dst = '\0';
    return r;
}
