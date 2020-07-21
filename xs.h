#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OFFSET sizeof(size_t)

typedef union {
    /* allow strings up to 15 bytes to stay on the stack
     * use the last byte as a null terminator and to store flags
     * much like fbstring:
     * https://github.com/facebook/folly/blob/master/folly/docs/FBString.md
     */
    char data[16];

    struct {
        uint8_t filler[15],
            /* how many free bytes in this stack allocated string
             * same idea as fbstring
             */
            space_left : 4,
            /* if it is on heap, set to 1 */
            is_ptr : 1, flag1 : 1, flag2 : 1, flag3 : 1;
    };

    /* heap allocated */
    struct {
        char *ptr;
        /* supports strings up to 2^54 - 1 bytes */
        size_t size : 54,
            /* capacity is always a power of 2 (unsigned)-1 */
            capacity : 6;
        /* the last 4 bits are important flags */
    };
} xs;

static inline bool xs_is_ptr(const xs *x)
{
    return x->is_ptr;
}
static inline size_t xs_size(const xs *x)
{
    return xs_is_ptr(x) ? x->size : (size_t) 15 - x->space_left;
}
static inline char *xs_data(const xs *x)
{
    return xs_is_ptr(x) ? (char *) x->ptr : (char *) x->data;
}
static inline size_t xs_capacity(const xs *x)
{
    return xs_is_ptr(x) ? ((size_t) 1 << x->capacity) - 1 : 15;
}

#define xs_literal_empty() \
    (xs) { .space_left = 15 }

static inline int ilog2(uint32_t n)
{
    return 32 - __builtin_clz(n) - 1;
}

/* Memory leaks happen if the string is too long but it is still useful for
 * short strings.
 * "" causes a compile-time error if x is not a string literal or too long.
 */
#define xs_tmp(x)                                          \
    ((void) ((struct {                                     \
         _Static_assert(sizeof(x) <= 16, "it is too big"); \
         int dummy;                                        \
     }){1}),                                               \
     xs_new(&xs_literal_empty(), "" x))

// Utility macro for refcnt
#define XS_INIT_REFCNT(x) *(size_t *) (x->ptr - OFFSET) = 1
#define XS_INCR_REFCNT(x) ++*(size_t *) (x->ptr - OFFSET)
#define XS_DECR_REFCNT(x) --*(size_t *) (x->ptr - OFFSET)
#define XS_GET_REFCNT(x) xs_is_ptr(x) ? *(size_t *) (x->ptr - OFFSET) : 1

static inline xs *xs_newempty(xs *x)
{
    *x = xs_literal_empty();
    return x;
}

static inline xs *xs_free(xs *x)
{
    int ref = XS_GET_REFCNT(x);
    if (ref > 1){
    	XS_DECR_REFCNT(x);
    }
    else if (xs_is_ptr(x)) {
	free(xs_data(x) - OFFSET);
    }

    return xs_newempty(x);
}


xs *xs_new(xs *x, const void *p);
void xs_cow(xs *x);
xs *xs_grow(xs *x, size_t len);
xs *xs_concat(xs *string, const xs *prefix, const xs *suffix);
xs *xs_trim(xs *x, const char *trimset);
xs *xs_cpy(xs *dest, xs *src);
char *xs_tok(xs *x, const char *delim);
