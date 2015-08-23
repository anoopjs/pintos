#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int fixed_point;

inline fixed_point fixed (int);
inline int fixed_to_int (fixed_point);
inline int fixed_to_int_near (fixed_point);
inline fixed_point add (fixed_point, fixed_point);
inline fixed_point sub (fixed_point, fixed_point);
inline fixed_point add_integer (fixed_point, int);
inline fixed_point sub_integer (fixed_point, int);
inline fixed_point mul (fixed_point, fixed_point);
inline fixed_point div (fixed_point, fixed_point);
inline fixed_point mul_integer (fixed_point, int);
inline fixed_point div_integer (fixed_point, int);

#endif
