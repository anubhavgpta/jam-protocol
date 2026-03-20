#ifndef MONITOR_H
#define MONITOR_H

#include "config.h"

/*
 * LinkMetrics — one snapshot of channel quality,
 * captured at the end of each packet transmission window.
 *
 * This is the raw data the Decision Engine reads. Think of
 * it as one row in a table of observations over time.
 */
typedef struct {
    float  pdr;             /* Packet Delivery Ratio (0.0 – 1.0)       */
    float  rss_dbm;         /* Received Signal Strength (dBm)           */
    int    retrans_count;   /* Retransmissions in this window           */
    int    packets_sent;    /* Packets attempted this window            */
    int    packets_recv;    /* Packets successfully received            */
    double timestamp;       /* Simulation time of this snapshot         */
} LinkMetrics;

/*
 * InterferenceStatus — classification result from the monitor.
 *
 * The critical insight: fading causes BOTH PDR and RSS to drop
 * together. Jamming causes PDR to drop while RSS stays stable
 * or even rises (jammer adds power to the channel).
 *
 * INTERFERENCE_NONE       — link is healthy
 * INTERFERENCE_SUSPECTED  — one bad window, could be fading
 * INTERFERENCE_CONFIRMED  — bad across PERSIST_WINDOWS, it's jamming
 * INTERFERENCE_FADING     — PDR and RSS both dropped (channel fade)
 */
typedef enum {
    INTERFERENCE_NONE      = 0,
    INTERFERENCE_SUSPECTED = 1,
    INTERFERENCE_CONFIRMED = 2,
    INTERFERENCE_FADING    = 3
} InterferenceStatus;

/*
 * MonitorState — the full sliding-window monitor.
 *
 * window[]         — circular buffer of recent LinkMetrics snapshots
 * window_head      — index of the oldest entry (circular buffer)
 * window_count     — how many valid entries are currently in the buffer
 * consecutive_bad  — count of consecutive windows below PDR_THRESHOLD
 * baseline_rss     — rolling average RSS used for fading-vs-jamming test
 */
typedef struct {
    LinkMetrics window[WINDOW_SIZE];
    int         window_head;
    int         window_count;
    int         consecutive_bad;
    float       baseline_rss;
} MonitorState;

/* ─── Public interface ──────────────────────────────── */

/* monitor_init — zero out the monitor before the simulation starts */
void monitor_init(MonitorState *m);

/*
 * monitor_update — push a new observation into the sliding window.
 * Overwrites the oldest entry once the buffer is full.
 */
void monitor_update(MonitorState *m, LinkMetrics obs);

/*
 * monitor_get_pdr — compute the PDR across the current window.
 * Returns: ratio of total received to total sent over all window entries.
 */
float monitor_get_pdr(const MonitorState *m);

/*
 * monitor_get_avg_rss — average RSS over the current window.
 */
float monitor_get_avg_rss(const MonitorState *m);

/*
 * monitor_get_avg_retrans — average retransmission count per window entry.
 */
float monitor_get_avg_retrans(const MonitorState *m);

/*
 * monitor_classify — run the interference detection algorithm.
 *
 * Logic:
 *   1. If window PDR >= PDR_THRESHOLD:  INTERFERENCE_NONE
 *   2. If PDR dropped AND RSS also dropped > RSS_DROP_TOLERANCE: FADING
 *   3. If PDR dropped but RSS stable:   SUSPECTED (first window)
 *   4. If SUSPECTED for PERSIST_WINDOWS consecutive windows: CONFIRMED
 */
InterferenceStatus monitor_classify(MonitorState *m);

#endif /* MONITOR_H */