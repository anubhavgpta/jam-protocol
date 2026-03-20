/*
 * test_monitor.c — standalone verification for monitor.c
 *
 * Tests four scenarios:
 *   1. Healthy link          → should classify as NONE
 *   2. Channel fading        → should classify as FADING
 *   3. Jamming (early)       → should classify as SUSPECTED
 *   4. Jamming (persistent)  → should classify as CONFIRMED
 *
 * Compile separately (not part of main build):
 *   gcc -Wall -std=c99 test_monitor.c monitor.c -o test_monitor -lm
 *   ./test_monitor
 */

#include <stdio.h>
#include <string.h>
#include "monitor.h"

/* Helper: convert enum to readable string */
static const char *status_name(InterferenceStatus s) {
    switch (s) {
        case INTERFERENCE_NONE:      return "NONE";
        case INTERFERENCE_SUSPECTED: return "SUSPECTED";
        case INTERFERENCE_CONFIRMED: return "CONFIRMED";
        case INTERFERENCE_FADING:    return "FADING";
        default:                     return "UNKNOWN";
    }
}

/*
 * fill_window — push N identical observations into the monitor.
 * Used to set up a known state before testing classify().
 */
static void fill_window(MonitorState *m,
                         float pdr, float rss,
                         int retrans, int count) {
    for (int i = 0; i < count; i++) {
        LinkMetrics obs;
        obs.pdr           = pdr;
        obs.rss_dbm       = rss;
        obs.retrans_count = retrans;
        obs.packets_sent  = 10;
        obs.packets_recv  = (int)(pdr * 10.0f);
        obs.timestamp     = (double)i * 0.1;
        monitor_update(m, obs);
    }
}

/* Helper: run a test case and print pass/fail */
static void run_test(const char *name,
                     InterferenceStatus got,
                     InterferenceStatus expected) {
    int pass = (got == expected);
    printf("  %-35s → got %-12s  %s\n",
           name,
           status_name(got),
           pass ? "PASS" : "FAIL ← check logic");
}

int main(void) {
    MonitorState m;
    InterferenceStatus result;

    printf("==============================================\n");
    printf("  Monitor Classification Test\n");
    printf("==============================================\n\n");

    /* ── Test 1: Healthy link ──────────────────────────────────
     * Push 20 observations with PDR=0.96, RSS=-50 dBm.
     * Expected: NONE (PDR is well above 0.75 threshold)
     */
    printf("Test 1 — Healthy link\n");
    monitor_init(&m);
    fill_window(&m, 0.96f, -50.0f, 0, WINDOW_SIZE);
    result = monitor_classify(&m);
    run_test("PDR=0.96, RSS=-50 dBm", result, INTERFERENCE_NONE);
    printf("  Window PDR : %.3f | Avg RSS : %.2f dBm | Baseline RSS : %.2f dBm\n\n",
           monitor_get_pdr(&m), monitor_get_avg_rss(&m), m.baseline_rss);

    /* ── Test 2: Channel fading ────────────────────────────────
     * First establish a good baseline, then push bad PDR AND
     * low RSS observations. RSS drops more than RSS_DROP_TOLERANCE.
     * Expected: FADING (both PDR and RSS fell together)
     */
    printf("Test 2 — Channel fading\n");
    monitor_init(&m);
    /* Establish baseline with good data */
    fill_window(&m, 0.96f, -50.0f, 0, WINDOW_SIZE);
    /* Now push fading observations: PDR bad + RSS also dropped */
    fill_window(&m, 0.60f, -72.0f, 2, WINDOW_SIZE);
    result = monitor_classify(&m);
    run_test("PDR=0.60, RSS dropped to -72", result, INTERFERENCE_FADING);
    printf("  RSS drop   : %.1f dBm (tolerance is %.1f dBm)\n",
           m.baseline_rss - monitor_get_avg_rss(&m), RSS_DROP_TOLERANCE);
    printf("  Window PDR : %.3f | Avg RSS : %.2f dBm | Baseline RSS : %.2f dBm\n\n",
           monitor_get_pdr(&m), monitor_get_avg_rss(&m), m.baseline_rss);

    /* ── Test 3: Jamming — first detection (SUSPECTED) ─────────
     * Establish baseline, then push ONE window of jammed data.
     * PDR drops but RSS stays flat (jammer signature).
     * consecutive_bad will be 1 < PERSIST_WINDOWS(3).
     * Expected: SUSPECTED
     */
    printf("Test 3 — Jamming, first window (suspected)\n");
    monitor_init(&m);
    fill_window(&m, 0.96f, -50.0f, 0, WINDOW_SIZE);
    /* Reset consecutive_bad so we're testing the first bad window */
    m.consecutive_bad = 0;
    /* Push bad PDR, RSS stable (jammer adding noise) */
    fill_window(&m, 0.50f, -48.0f, 6, WINDOW_SIZE);
    /* Force only 1 bad window count */
    m.consecutive_bad = 0;
    result = monitor_classify(&m);
    run_test("PDR=0.50, RSS stable at -48", result, INTERFERENCE_SUSPECTED);
    printf("  consecutive_bad : %d (need %d for CONFIRMED)\n\n",
           m.consecutive_bad, PERSIST_WINDOWS);

    /* ── Test 4: Jamming — persistent (CONFIRMED) ──────────────
     * Same as Test 3 but consecutive_bad is already at threshold.
     * Expected: CONFIRMED
     */
    printf("Test 4 — Jamming, persistent (confirmed)\n");
    monitor_init(&m);
    fill_window(&m, 0.96f, -50.0f, 0, WINDOW_SIZE);
    fill_window(&m, 0.50f, -48.0f, 6, WINDOW_SIZE);
    /* Simulate that we already saw 2 bad windows before this one */
    m.consecutive_bad = PERSIST_WINDOWS - 1;
    result = monitor_classify(&m);
    run_test("PDR=0.50, RSS stable, 3rd bad window", result, INTERFERENCE_CONFIRMED);
    printf("  consecutive_bad : %d\n\n", m.consecutive_bad);

    /* ── Test 5: PDR metrics accuracy ──────────────────────────
     * Verify monitor_get_pdr() sums correctly across the window.
     * Push windows with known sent/recv counts and check the math.
     */
    printf("Test 5 — PDR computation accuracy\n");
    monitor_init(&m);
    /* Push 10 observations: 10 sent, 8 received each = PDR 0.80 */
    for (int i = 0; i < 10; i++) {
        LinkMetrics obs = {0};
        obs.packets_sent = 10;
        obs.packets_recv = 8;
        obs.pdr          = 0.8f;
        obs.rss_dbm      = -50.0f;
        monitor_update(&m, obs);
    }
    float pdr = monitor_get_pdr(&m);
    int pass = (pdr > 0.799f && pdr < 0.801f);
    printf("  %-35s → got %.4f      %s\n",
           "10 windows at 8/10 recv", pdr, pass ? "PASS" : "FAIL");

    /* ── Test 6: EMA baseline update ───────────────────────────
     * Verify baseline_rss only updates on healthy windows.
     * Push jammed data and confirm baseline doesn't drift up.
     */
    printf("\nTest 6 — Baseline RSS protection during jamming\n");
    monitor_init(&m);
    float initial_baseline = m.baseline_rss;
    /* Push bad PDR + elevated RSS (jammer signature) */
    fill_window(&m, 0.40f, -35.0f, 8, WINDOW_SIZE);
    float after_jam_baseline = m.baseline_rss;
    int protected = (after_jam_baseline <= initial_baseline + 1.0f);
    printf("  Baseline before : %.2f dBm\n", initial_baseline);
    printf("  Baseline after  : %.2f dBm  (jammer RSS was -35.0)\n", after_jam_baseline);
    printf("  %-35s → %s\n",
           "Baseline unchanged during jamming",
           protected ? "PASS" : "FAIL");

    printf("\n==============================================\n");
    printf("  All tests complete.\n");
    printf("==============================================\n");
    return 0;
}