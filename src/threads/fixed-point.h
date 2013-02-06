#include <debug.h>

#define P 17
#define Q 14
#define F (1 << Q)

typedef int fp;

ASSERT (P + Q == 31);

static inline fp
fp_convert(int n)
{
  return (fp) (n * F);
}

static inline int
fp_toint(fp x)
{
  return x / F;
}

static inline int
fp_toint_round(fp x)
{
  if (x >= 0)
    return (x + F / 2) / F;
  else
    return (x - F / 2) / F;
}

static inline fp
fp_add(fp a, fp b)
{
  return (fp) (a + b);
}

static inline fp
fp_add_int(fp a, int n)
{
  return (fp) (a + fp_convert(n));
}

static inline fp
fp_subtract(fp a, fp b)
{
  return (fp) (a - b);
}

static inline fp
fb_subtract_int(fp a, int n)
{
  return (fp) (a - fp_convert(n));
}

static inline fp
fb_multiply(fp a, fp b)
{
  return (fp) (((int64_t) a) * b / F);
}

static inline fp
fp_multiply_int(fp a, int n)
{
  return (fp) (a * n);
}

static inline fp
fp_divide(fp a, fp b)
{
  return (fp) (((int64_t) a) * F / b);
}

static inline fp
fp_divide_int(fp a, int n)
{
  return (fp) (a / n);
}

// vim:ts=2:sw=2:et
