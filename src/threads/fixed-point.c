#include "threads/fixed-point.h"
#include <stdint.h>
#define P 17
#define Q 14
#define F (2 << Q)

fixed_point fixed (int x)
{
  return (x * F);
}

int fixed_to_int (fixed_point x)
{
  return x / F;
}

int fixed_to_int_near (fixed_point x)
{
  if (x >= 0)
    return (x + F / 2) / F;
  else
    return (x - F / 2) / F;
}

fixed_point add (fixed_point x, fixed_point y)
{
  return x + y;
}

fixed_point sub (fixed_point x, fixed_point y)
{
  return x - y;
}

fixed_point add_integer (fixed_point x, int n)
{
  return x + n * F;
}

fixed_point sub_integer (fixed_point x, int n)
{
  return x - n * F;
}

fixed_point mul (fixed_point x, fixed_point y)
{
  return ((int64_t) x) * y / F;
}

fixed_point div (fixed_point x, fixed_point y)
{
  return ((int64_t) x) * F / y;
}

fixed_point mul_integer (fixed_point x, int n)
{
  return x * n;
}

fixed_point div_integer (fixed_point x, int n)
{
  return x / n;
}
