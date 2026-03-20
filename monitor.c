/*
 * monitor.c — Link Quality Monitor
 *
 * Implements a circular sliding window over LinkMetrics observations.
 * The key function is monitor_classify(), which distinguishes between:
 *   - Normal operation   (PDR is healthy)
 *   - Channel fading     (PDR dropped AND RSS dropped — correlated)
 *   - Jamming suspected  (PDR dropped, RSS stable — first window)
 *   - Jamming confirmed  (above, persisted across PERSIST_WINDOWS)
 *
 * This distinction is the core contribution of the protocol.
 * Channel fading requires no action (it self-recovers). Jamming
 * requires the adaptive response from adaptive.c.
 */

#include <string.h>   /* memset */
#include <math.h>     /* fabsf  */
#include <stdio.h>    /* printf (used in debug prints) */
#include "monitor.h"

/* ─── Internal helpers ───────────────────────────────────────────── */

/*
 * window_entry — returns a pointer to slot i in the circular buffer.
 *
 * The buffer is circular: when window_head reaches WINDOW_SIZE,
 * it wraps back to 0, overwriting the oldest entry.
 *
 * Visual:
 *   [0][1][2]...[head]...[WINDOW_SIZE-1]
 *                  ↑ next write goes here, then head advances
 */
static LinkMetrics *window_entry(MonitorState *m, int i) {
    return &m->window[i % WINDOW_SIZE];
}

/* ─── Public functions ───────────────────────────────────────────── */

/*
 * monitor_init — zero everything out before simulation starts.
 *
 * baseline_rss is set to RSS_BASELINE (from config.h = -50 dBm).
 * This is the RSS we expect on a perfectly clean channel.
 * As observations accumulate, it updates toward the true average.
 */
void monitor_init(MonitorState *m) {
    memset(m, 0, sizeof(MonitorState));
    m->window_head     = 0;
    m->window_count    = 0;
    m->consecutive_bad = 0;
    m->baseline_rss    = RSS_BASELINE;
}

/*
 * monitor_update — push one new observation into the sliding window.
 *
 * How the circular buffer works:
 *   - window_head always points to the slot we write NEXT.
 *   - After writing, we advance head by 1 (mod WINDOW_SIZE).
 *   - window_count tracks how many valid entries exist (caps at WINDOW_SIZE).
 *
 * Example with WINDOW_SIZE=5, after 7 insertions:
 *   Insertions: A B C D E F G
 *   Buffer:    [F][G][C][D][E]   ← C,D,E are older; F,G are newest
 *   head = 2  (next write goes to slot 2, overwriting C)
 *
 * baseline_rss update:
 *   We use an exponential moving average (EMA) with alpha=0.1.
 *   EMA is better than a simple average because it weights recent
 *   observations more heavily. Formula:
 *     baseline = 0.9 × baseline + 0.1 × new_rss
 *   This means after ~20 windows, baseline reflects current conditions.
 *   We only update baseline when the link looks healthy (PDR > PDR_GOOD)
 *   so that jamming events don't corrupt our reference point.
 */
void monitor_update(MonitorState *m, LinkMetrics obs) {
    /* Write to the current head slot */
    *window_entry(m, m->window_head) = obs;

    /* Advance head, wrapping around at WINDOW_SIZE */
    m->window_head = (m->window_head + 1) % WINDOW_SIZE;

    /* Count valid entries (stops incrementing once buffer is full) */
    if (m->window_count < WINDOW_SIZE) {
        m->window_count++;
    }

    /*
     * Update baseline RSS only when the link is healthy.
     * If we updated during jamming, baseline_rss would drift upward
     * (jammers raise RSS), making future jamming harder to detect.
     */
    if (obs.pdr >= PDR_GOOD) {
        /* EMA: 90% old value + 10% new observation */
        m->baseline_rss = 0.9f * m->baseline_rss + 0.1f * obs.rss_dbm;
    }
}

/*
 * monitor_get_pdr — compute PDR across the entire current window.
 *
 * We sum total packets received and total packets sent across all
 * valid window entries, then divide. This is more accurate than
 * averaging the per-window PDR values, because window sizes can
 * vary slightly.
 *
 * Example:
 *   Window has 3 entries: {10 sent, 9 recv}, {10 sent, 4 recv}, {10 sent, 8 recv}
 *   Correct:  (9+4+8) / (10+10+10) = 21/30 = 0.70
 *   Wrong:    (0.9 + 0.4 + 0.8) / 3 = 0.70  (same here, but differs
 *             when packet counts vary between windows)
 */
float monitor_get_pdr(const MonitorState *m) {
    if (m->window_count == 0) return 1.0f;  /* no data = assume clean */

    int total_sent = 0;
    int total_recv = 0;

    for (int i = 0; i < m->window_count; i++) {
        const LinkMetrics *e = &m->window[i];
        total_sent += e->packets_sent;
        total_recv += e->packets_recv;
    }

    if (total_sent == 0) return 1.0f;
    return (float)total_recv / (float)total_sent;
}

/*
 * monitor_get_avg_rss — arithmetic mean of RSS over the window.
 *
 * RSS is in dBm (logarithmic scale). Averaging dBm values is an
 * approximation — the technically correct method averages linear
 * power (mW), then converts back. However, for the purpose of
 * interference detection (where we care about direction of change,
 * not exact power), dBm averaging is sufficient and much simpler.
 */
float monitor_get_avg_rss(const MonitorState *m) {
    if (m->window_count == 0) return RSS_BASELINE;

    float sum = 0.0f;
    for (int i = 0; i < m->window_count; i++) {
        sum += m->window[i].rss_dbm;
    }
    return sum / (float)m->window_count;
}

/*
 * monitor_get_avg_retrans — mean retransmission count per window entry.
 *
 * A sudden spike here — especially when PDR is also dropping —
 * is a strong indicator of reactive or random jamming.
 */
float monitor_get_avg_retrans(const MonitorState *m) {
    if (m->window_count == 0) return 0.0f;

    int total = 0;
    for (int i = 0; i < m->window_count; i++) {
        total += m->window[i].retrans_count;
    }
    return (float)total / (float)m->window_count;
}

/*
 * monitor_classify — the interference detection algorithm.
 *
 * This function is called once per window by adaptive_control().
 * It implements the three-gate logic shown in the architecture diagram:
 *
 * Gate 1 — Is PDR acceptable?
 *   If window PDR >= PDR_THRESHOLD (0.75): link is healthy, reset
 *   the consecutive_bad counter and return INTERFERENCE_NONE.
 *
 * Gate 2 — Is this fading or jamming?
 *   Compare current avg RSS against stored baseline_rss.
 *   If RSS dropped by more than RSS_DROP_TOLERANCE (10 dBm):
 *     → Both PDR and RSS fell together → channel fading.
 *     Return INTERFERENCE_FADING. No adaptive action needed —
 *     fading is transient and self-resolves.
 *   If RSS is stable (within tolerance) while PDR dropped:
 *     → Jammer is adding noise. PDR suffers but RSS stays up
 *       (or rises) because the jammer adds RF energy.
 *     → Proceed to Gate 3.
 *
 * Gate 3 — Is this persistent?
 *   Increment consecutive_bad. If it has reached PERSIST_WINDOWS (3):
 *     → Pattern has held across multiple windows → jamming confirmed.
 *     Return INTERFERENCE_CONFIRMED.
 *   Otherwise: first or second bad window — could still be transient.
 *     Return INTERFERENCE_SUSPECTED.
 *
 * Why three gates instead of just checking PDR?
 *   A single PDR threshold would trigger on every brief fade,
 *   causing the node to switch channels unnecessarily and waste
 *   power. The three-gate approach gives us:
 *     - Zero false positives from fading (Gate 2 catches them)
 *     - Low false positives from noise bursts (Gate 3 catches them)
 *     - Fast response to real jamming (3 windows ≈ 0.6 seconds)
 */
InterferenceStatus monitor_classify(MonitorState *m) {
    /* Need at least one full window before making decisions */
    if (m->window_count < WINDOW_SIZE / 2) {
        return INTERFERENCE_NONE;
    }

    float current_pdr = monitor_get_pdr(m);
    float current_rss = monitor_get_avg_rss(m);

    /* ── Gate 1: PDR check ─────────────────────────────────────── */
    if (current_pdr >= PDR_THRESHOLD) {
        /* Link is healthy — reset the bad window counter */
        m->consecutive_bad = 0;
        return INTERFERENCE_NONE;
    }

    /*
     * PDR is below threshold. Now determine WHY.
     *
     * ── Gate 2: Fading vs jamming ──────────────────────────────
     *
     * rss_drop is positive when RSS fell (signal got weaker).
     * rss_drop is negative when RSS rose (jammer added energy).
     *
     * If rss_drop > RSS_DROP_TOLERANCE: signal weakened → fading.
     * If rss_drop <= RSS_DROP_TOLERANCE: signal stable → jamming.
     */
    float rss_drop = m->baseline_rss - current_rss;

    if (rss_drop > RSS_DROP_TOLERANCE) {
        /*
         * RSS dropped alongside PDR — this is channel fading.
         * Do NOT increment consecutive_bad, because fading is not
         * something we can fix by switching channels or adjusting power.
         * The adaptive engine will do nothing and wait for recovery.
         */
        return INTERFERENCE_FADING;
    }

    /*
     * ── Gate 3: Persistence check ──────────────────────────────
     * RSS is stable but PDR is bad → this looks like jamming.
     * Count how many consecutive windows have shown this pattern.
     */
    m->consecutive_bad++;

    if (m->consecutive_bad >= PERSIST_WINDOWS) {
        return INTERFERENCE_CONFIRMED;
    }

    return INTERFERENCE_SUSPECTED;
}