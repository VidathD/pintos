#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

#define FP_SHIFT 14
#define FP_ONE (1 << FP_SHIFT)

static inline int
fp_mul (int a, int b)
{
  return (int) (((int64_t) a) * b / FP_ONE);
}

static inline int
fp_div (int a, int b)
{
  return (int) (((int64_t) a) * FP_ONE / b);
}

static inline int
fp_to_int (int value)
{
  return value / FP_ONE;
}

static inline int
fp_to_int_nearest (int value)
{
  if (value >= 0)
    return (value + FP_ONE / 2) / FP_ONE;
  return (value - FP_ONE / 2) / FP_ONE;
}

#endif /* threads/fixed-point.h */