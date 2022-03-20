#ifndef UTIL_TIME_H_GUARD_
#define UTIL_TIME_H_GUARD_

typedef enum
{
    UTIL_CLOCK_FASTER = 0,
    UTIL_CLOCK_TODO = 1
}
clock_type_e;

int util_clock_set_type(clock_type_e clock_type);
int util_clock_gettime(struct timespec *tp);

#endif /* UTIL_TIME_H_GUARD_ */

