#include <math.h>
#include <stdio.h>
#include <windows.h>

double monotonic_seconds(void);

int main(void)
{
    ULONGLONG ticks_before = GetTickCount64();
    double before = monotonic_seconds();
    Sleep(100);
    double after = monotonic_seconds();
    ULONGLONG ticks_after = GetTickCount64();
    double elapsed = after - before;
    double reference = (ticks_after - ticks_before) / 1000.0;

    if (!isfinite(before) || !isfinite(after) || before < 0.0 ||
        elapsed <= 0.0 || reference <= 0.0 ||
        elapsed < reference / 4.0 || elapsed > reference * 4.0)
    {
        fprintf(stderr, "Invalid Hamlib monotonic clock: before=%g after=%g "
                "elapsed=%g reference=%g seconds\n",
                before, after, elapsed, reference);
        return 1;
    }

    printf("Hamlib monotonic clock advanced %g seconds (reference %g)\n",
           elapsed, reference);
    return 0;
}
