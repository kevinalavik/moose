#ifndef LIB_MATH_H
#define LIB_MATH_H

#define ALIGN_UP(x, y) (((x) + ((y) - 1)) & ~((y) - 1))
#define ALIGN_DOWN(x, y) ((x) & ~((y) - 1))
#define IS_ALIGNED(x, y) (((x) & ((y) - 1)) == 0)
#define IS_NOT_ALIGNED(x, y) (((x) & ((y) - 1)) != 0)

#endif /* LIB_MATH_H */