/*
 * test_jammer.c — standalone test for jammer.c
 *
 * Compile and run independently:
 *   gcc -Wall -std=c99 test_jammer.c jammer.c -o test_jammer
 *   ./test_jammer
 *
 * This is NOT part of the main build. It's a verification tool.
 * Remove it (or keep it) — it does not affect the main project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "jammer.h"

#define TEST_SAMPLES 50   /* packets to simulate per jammer type */

static void run_test(const char *name, JammerType type, float intensity) {
    JammerModel j;
    jammer_init(&j, type, intensity);

    float base_pdr = 0.98f;     /* clean channel PDR */
    float base_rss = -50.0f;    /* clean channel RSS (dBm) */

    float total_pdr = 0.0f;
    float total_rss = 0.0f;
    int   active_count = 0;

    printf("\n── %s jammer (intensity=%.1f) ──\n", name, intensity);
    printf("  %-6s  %-8s  %-8s  %-8s\n",
           "Sample", "Active", "PDR", "RSS(dBm)");
    printf("  ──────────────────────────────────\n");

    for (int i = 0; i < TEST_SAMPLES; i++) {
        float out_pdr, out_rss;
        /* transmitting = 1 always for this test */
        jammer_apply(&j, base_pdr, base_rss, 1, &out_pdr, &out_rss);

        total_pdr   += out_pdr;
        total_rss   += out_rss;
        active_count += j.active;

        /* Print first 10 samples so you can see individual behaviour */
        if (i < 10) {
            printf("  %-6d  %-8s  %-8.3f  %-8.2f\n",
                   i+1, j.active ? "YES" : "no", out_pdr, out_rss);
        }
    }

    printf("  ... (%d more samples)\n", TEST_SAMPLES - 10);
    printf("  ──────────────────────────────────\n");
    printf("  Avg PDR      : %.4f  (base was %.4f)\n",
           total_pdr / TEST_SAMPLES, base_pdr);
    printf("  Avg RSS      : %.2f dBm  (base was %.2f dBm)\n",
           total_rss / TEST_SAMPLES, base_rss);
    printf("  Active pct   : %.1f%%\n",
           100.0f * active_count / TEST_SAMPLES);
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("=========================================\n");
    printf("  Jammer Model Verification Test\n");
    printf("=========================================\n");
    printf("Base PDR: 0.98 | Base RSS: -50.0 dBm\n");
    printf("Expected behaviour:\n");
    printf("  Constant : PDR ~0.39, RSS rises, always active\n");
    printf("  Random   : PDR fluctuates, active ~40%% of time\n");
    printf("  Reactive : PDR crashes when transmitting\n");
    printf("  None     : PDR and RSS unchanged\n");

    run_test("None",     JAMMER_NONE,     1.0f);
    run_test("Constant", JAMMER_CONSTANT, 1.0f);
    run_test("Random",   JAMMER_RANDOM,   1.0f);
    run_test("Reactive", JAMMER_REACTIVE, 1.0f);

    printf("\n=========================================\n");
    printf("  RSS vs fading verification\n");
    printf("  Jamming raises RSS. Fading lowers it.\n");
    printf("=========================================\n");

    JammerModel j;
    float out_pdr, out_rss;

    /* Simulate jamming (RSS should rise) */
    jammer_init(&j, JAMMER_CONSTANT, 1.0f);
    jammer_apply(&j, 0.98f, -50.0f, 1, &out_pdr, &out_rss);
    printf("Constant jam  → PDR: %.3f | RSS: %.2f dBm (rose by %.1f)\n",
           out_pdr, out_rss, out_rss - (-50.0f));

    /* Simulate fading manually (RSS drops, PDR also drops) */
    float faded_rss = -68.0f;
    float faded_pdr = 0.71f;
    jammer_init(&j, JAMMER_NONE, 0.0f);
    jammer_apply(&j, faded_pdr, faded_rss, 1, &out_pdr, &out_rss);
    printf("Channel fading → PDR: %.3f | RSS: %.2f dBm (dropped by %.1f)\n",
           out_pdr, out_rss, out_rss - (-50.0f));

    printf("\nVerification complete.\n");
    system("pause");
    return 0;
}