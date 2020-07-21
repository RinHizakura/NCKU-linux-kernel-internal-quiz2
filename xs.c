#include "xs.h"
#include <time.h>

#define COPYS 300000

#ifdef NOCOW
#define COW_MODE 0
#else
#define COW_MODE 1
#endif

xs *xs_new(xs *x, const void *p)
{
    *x = xs_literal_empty();
    size_t len = strlen(p) + 1;
    if (len > 16) {
        x->capacity = ilog2(len) + 1;
        x->size = len - 1;
        x->is_ptr = true;
        x->ptr = malloc(((size_t) 1 << x->capacity) + OFFSET) + OFFSET;
        memcpy(x->ptr, p, len);
        XS_INIT_REFCNT(x);
    } else {
        memcpy(x->data, p, len);
        x->space_left = 15 - (len - 1);
    }
    return x;
}

/* grow up to specified size */
xs *xs_grow(xs *x, size_t len)
{
    if (len <= xs_capacity(x))
        return x;
    len = ilog2(len) + 1;
    if (xs_is_ptr(x))
        x->ptr = realloc(x->ptr, ((size_t) 1 << len) + OFFSET) + OFFSET;
    else {
        char buf[16];
        memcpy(buf, x->data, 16);
        x->ptr = malloc(((size_t) 1 << len) + OFFSET) + OFFSET;
        memcpy(x->ptr, buf, 16);
    }
    x->is_ptr = true;
    x->capacity = len;
    XS_INIT_REFCNT(x);
    return x;
}

void xs_cow(xs *x)
{
    size_t ref = XS_GET_REFCNT(x);
    if (ref > 1) {
        XS_DECR_REFCNT(x);
        xs_new(x, xs_data(x));
    }
}

xs *xs_concat(xs *string, const xs *prefix, const xs *suffix)
{
    // before we start, check if our string is a COW string
    xs_cow(string);

    size_t pres = xs_size(prefix), sufs = xs_size(suffix),
           size = xs_size(string), capacity = xs_capacity(string);

    char *pre = xs_data(prefix), *suf = xs_data(suffix),
         *data = xs_data(string);

    if (size + pres + sufs <= capacity) {
        memmove(data + pres, data, size);
        memcpy(data, pre, pres);
        memcpy(data + pres + size, suf, sufs + 1);
        string->space_left = 15 - (size + pres + sufs);
    } else {
        xs tmps = xs_literal_empty();
        xs_grow(&tmps, size + pres + sufs);
        char *tmpdata = xs_data(&tmps);
        memcpy(tmpdata + pres, data, size);
        memcpy(tmpdata, pre, pres);
        memcpy(tmpdata + pres + size, suf, sufs + 1);
        xs_free(string);
        *string = tmps;
        string->size = size + pres + sufs;
    }
    return string;
}

xs *xs_trim(xs *x, const char *trimset)
{
    if (!trimset[0])
        return x;

    xs_cow(x);

    char *dataptr = xs_data(x), *orig = dataptr;

    /* similar to strspn/strpbrk but it operates on binary data */
    uint8_t mask[32] = {0};

#define check_bit(byte) (mask[(uint8_t) byte / 8] & 1 << (uint8_t) byte % 8)
#define set_bit(byte) (mask[(uint8_t) byte / 8] |= 1 << (uint8_t) byte % 8)

    size_t i, slen = xs_size(x), trimlen = strlen(trimset);

    for (i = 0; i < trimlen; i++)
        set_bit(trimset[i]);
    for (i = 0; i < slen; i++)
        if (!check_bit(dataptr[i]))
            break;
    for (; slen > 0; slen--)
        if (!check_bit(dataptr[slen - 1]))
            break;
    dataptr += i;
    slen -= i;

    /* reserved space as a buffer on the heap.
     * Do not reallocate immediately. Instead, reuse it as possible.
     * Do not shrink to in place if < 16 bytes.
     */
    memmove(orig, dataptr, slen);
    /* do not dirty memory unless it is needed */
    if (orig[slen])
        orig[slen] = 0;

    if (xs_is_ptr(x))
        x->size = slen;
    else
        x->space_left = 15 - slen;
    return x;
#undef check_bit
#undef set_bit
}

xs *xs_cpy(xs *dest, xs *src)
{
    // If dest was referenced to others, decrease the reference count first
    size_t ref = XS_GET_REFCNT(dest);
    if (ref > 1)
        XS_DECR_REFCNT(dest);

    // If src is long string, just make a copy
    if (COW_MODE && xs_is_ptr(src)) {
        dest->is_ptr = true;
        XS_INCR_REFCNT(src);
        *dest = *src;
    }
    // Else just simply memcpy
    else {
        dest->is_ptr = false;  // set dest's flag to short string
        size_t len = xs_size(src);
        xs_grow(dest, len);
        memcpy(xs_data(dest), xs_data(src), len);
        if (!xs_is_ptr(dest))
            dest->space_left = 15 - len;
        else
            dest->size = len;
    }
    return dest;
}

char *xs_tok(xs *x, const char *delim)
{
    if (!delim[0])
        return xs_data(x);

    static char *save_ptr;
    char *end;
    char *s;

    if (x == NULL)
        s = save_ptr;
    else {
        xs_cow(x);
        s = xs_data(x);
    }

    if (*s == '\0') {
        save_ptr = s;
        return NULL;
    }

    uint8_t mask[32] = {0};

#define check_bit(byte) (mask[(uint8_t) byte / 8] & 1 << (uint8_t) byte % 8)
#define set_bit(byte) (mask[(uint8_t) byte / 8] |= 1 << (uint8_t) byte % 8)

    size_t i, slen = strlen(s), delimlen = strlen(delim);

    for (i = 0; i < delimlen; i++)
        set_bit(delim[i]);
    /* Scan leading delimiters.  */
    for (i = 0; i < slen; i++)
        if (check_bit(s[i]))
            break;

    if (*s == '\0') {
        save_ptr = s;
        return NULL;
    }

    end = s + i;
    if (*end == '\0') {
        save_ptr = end;
        return s;
    }

    /* Terminate the token and make *SAVE_PTR point past it.  */
    *end = '\0';
    save_ptr = end + 1;

    // Revise structure member of x if it is not NULL
    if (x != NULL) {
        if (xs_is_ptr(x))
            x->size = i;
        else
            x->space_left = 15 - i;
    }
    return s;

#undef check_bit
#undef set_bit
}

void test_cpy()
{
    printf("===== Test1 =====\n");
    xs src1, s1, s2;
    xs_newempty(&src1);
    xs_newempty(&s1);
    xs_newempty(&s2);

    xs_new(&src1, "Happy Lucky Smile Yeah!!!!!!");
    xs_cpy(&s1, &src1);
    printf("Befor cpy to s2, s1: %s %ld\n", xs_data(&s1), XS_GET_REFCNT((&s1)));
    xs_cpy(&s2, &src1);
    printf("After cpy to s2, s1: %s %ld\n", xs_data(&s1), XS_GET_REFCNT((&s1)));
    printf("Now s2: %s %ld\n", xs_data(&s2), XS_GET_REFCNT((&s2)));

    printf("===== Test2 =====\n");
    xs prefix = *xs_tmp("((("), suffix = *xs_tmp(")))");
    xs_concat(&s2, &prefix, &suffix);
    printf("After concat to s2, s1: %s %ld\n", xs_data(&s1),
           XS_GET_REFCNT((&s1)));

    printf("===== Test3 =====\n");
    xs_trim(&s2, "()!");
    printf("After trim s2, s2: %s %ld\n", xs_data(&s2), XS_GET_REFCNT((&s2)));


    // After test, free it
    xs_free(&src1);
    xs_free(&s2);
    xs_free(&s1);
}

void test_tok()
{
    xs string;
    xs_newempty(&string);

    const char s[2] = "-";
    char *token;
    xs_new(&string, "This is - www.gitbook.net - website");

    /* get the first token */
    token = xs_tok(&string, s);

    /* walk through other tokens */
    while (token != NULL) {
        printf("token: %s\n", token);
        token = xs_tok(NULL, s);
    }
    xs_free(&string);
}

void test_perf()
{
    xs src;
    xs_new(&src,
           "This is a long long long long long long long long long \
			long long long long long long long long string");

    xs str[COPYS];
    // init the xstring array first
    for (int i = 0; i < COPYS; i++)
        xs_newempty(&str[i]);

    clock_t start, end;
    start = clock();
    for (int i = 0; i < COPYS; i++)
        xs_cpy(&str[i], &src);
    end = clock();

    double time = ((double) (end - start)) / CLOCKS_PER_SEC;
    if (COW_MODE)
        printf("Mode COW, using time %lf\n", time);
    else
        printf("Mode NO COW, using time %lf\n", time);
}
int main()
{
    // test_cpy();
    // test_tok();
    test_perf();

    return 0;
}
