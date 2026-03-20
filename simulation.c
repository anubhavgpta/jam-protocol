/*
 * simulation.c — Simulation Engine
 *
 * Runs all 8 scenarios (4 jammer types × adaptive + baseline),
 * collects per-window metrics, writes CSV output files, and
 * produces the comparison table used in the research paper.
 *
 * Scenario naming convention: "Adaptive_Constant", "Baseline_Random" etc.
 * Each run produces one row per window in the per-run CSV, and one
 * summary row in results_comparison.csv.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "simulation.h"

/* ─── Internal: channel quality model ───────────────────────────────
 *
 * Real wireless channels aren't perfectly clean even without a jammer.
 * We model baseline channel quality using a simple path-loss + small
 * random variation. This gives more realistic PDR curves.
 *
 * base_channel_pdr() returns the expected PDR on a clean channel,
 * incorporating a small Gaussian-like variation (±0.02) using the
 * Box-Muller approximation via summed uniforms.
 *
 * base_channel_rss() returns the expected RSS (dBm) with a small
 * variation (±2 dBm) to simulate real multipath fluctuation.
 */
static float base_channel_pdr(void) {
    /* Sum 4 uniform(0,1) and normalise → approximate Gaussian */
    float u = 0.0f;
    for (int i = 0; i < 4; i++) u += (float)rand() / (float)RAND_MAX;
    /* u is in [0,4], mean=2, std≈0.58. Scale to ±0.02 around 0.98 */
    float variation = (u - 2.0f) * 0.017f;
    float pdr = 0.98f + variation;
    if (pdr > 1.0f) pdr = 1.0f;
    if (pdr < 0.0f) pdr = 0.0f;
    return pdr;
}

static float base_channel_rss(void) {
    float u = 0.0f;
    for (int i = 0; i < 4; i++) u += (float)rand() / (float)RAND_MAX;
    float variation = (u - 2.0f) * 1.7f;   /* ±2 dBm around baseline */
    return RSS_BASELINE + variation;
}

/* ─── Internal: throughput calculation ──────────────────────────────
 *
 * Throughput = successfully delivered packets per second.
 * packet_rate is packets attempted per second, so:
 *   throughput = pdr × packet_rate
 *
 * We model each packet as carrying a fixed payload (assume 512 bytes)
 * to give throughput in bytes/sec if needed, but we report in pkt/sec
 * for simplicity.
 */
static float calc_throughput(float pdr, int packet_rate) {
    return pdr * (float)packet_rate;
}

/* ─── CSV functions ──────────────────────────────────────────────── */

int sim_write_csv_header(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", filename);
        return -1;
    }
    fprintf(f, "window,pdr,rss_dbm,retrans,packets_sent,packets_recv,"
               "throughput,channel,tx_power_dbm,packet_rate,action\n");
    fclose(f);
    return 0;
}

void sim_write_csv_row(const char *filename, int window_num,
                       const LinkMetrics *m, int channel,
                       float tx_power, int packet_rate,
                       int action_taken) {
    FILE *f = fopen(filename, "a");   /* append mode */
    if (!f) return;

    float throughput = calc_throughput(m->pdr, packet_rate);

    fprintf(f, "%d,%.4f,%.2f,%d,%d,%d,%.4f,%d,%.1f,%d,%d\n",
            window_num,
            m->pdr,
            m->rss_dbm,
            m->retrans_count,
            m->packets_sent,
            m->packets_recv,
            throughput,
            channel,
            tx_power,
            packet_rate,
            action_taken);
    fclose(f);
}

void sim_write_comparison(const SimResult *results, int count) {
    FILE *f = fopen(OUTPUT_CSV_COMPARISON, "w");
    if (!f) {
        fprintf(stderr, "Error: cannot open comparison CSV\n");
        return;
    }

    /* Header */
    fprintf(f, "scenario,avg_pdr,avg_throughput,avg_rss_dbm,"
               "total_retrans,recovery_time_s,interference_events,"
               "total_sent,total_recv\n");

    for (int i = 0; i < count; i++) {
        const SimResult *r = &results[i];
        fprintf(f, "%s,%.4f,%.4f,%.2f,%d,%.2f,%d,%d,%d\n",
                r->scenario,
                r->avg_pdr,
                r->avg_throughput,
                r->avg_rss_dbm,
                r->total_retrans,
                r->recovery_time,
                r->interference_events,
                r->total_packets_sent,
                r->total_packets_recv);
    }
    fclose(f);
}

void sim_print_result(const SimResult *r) {
    printf("  Scenario         : %s\n",   r->scenario);
    printf("  Avg PDR          : %.4f\n", r->avg_pdr);
    printf("  Avg Throughput   : %.4f pkt/s\n", r->avg_throughput);
    printf("  Avg RSS          : %.2f dBm\n", r->avg_rss_dbm);
    printf("  Total retrans    : %d\n",   r->total_retrans);
    printf("  Recovery time    : %.2f s\n", r->recovery_time);
    printf("  Interference evt : %d\n",   r->interference_events);
    printf("  Packets sent     : %d\n",   r->total_packets_sent);
    printf("  Packets recv     : %d\n",   r->total_packets_recv);
}

/* ─── sim_run ────────────────────────────────────────────────────────
 *
 * Execute one complete simulation run for a given RadioNode.
 *
 * The loop runs TOTAL_PACKETS iterations. Each iteration:
 *   1. Compute base channel quality (PDR, RSS) with small variation
 *   2. Apply jammer effects via jammer_apply()
 *   3. Decide per-packet outcome (received or dropped) using rand()
 *   4. Accumulate into current window counts
 *   5. Every WINDOW_SIZE packets: close the window, push to monitor,
 *      call adaptive_control(), log to CSV
 *
 * Recovery time measurement:
 *   We record the simulation time when interference is first CONFIRMED,
 *   and again when PDR recovers above PDR_GOOD. The difference is the
 *   recovery time — one of the key metrics in the paper.
 */
void sim_run(RadioNode *node, SimResult *result) {
    /* Determine output CSV filename from node type and jammer */
    char filename[128];
    snprintf(filename, sizeof(filename), "%s",
             node->is_adaptive ? OUTPUT_CSV_ADAPTIVE : OUTPUT_CSV_BASELINE);

    /* Write header (overwrites any previous run's file) */
    sim_write_csv_header(filename);

    /* Initialise result struct */
    memset(result, 0, sizeof(SimResult));
    snprintf(result->scenario, sizeof(result->scenario), "%s_%s",
             node->is_adaptive ? "Adaptive" : "Baseline",
             JAMMER_NAMES[node->jammer.type]);

    /* Per-window accumulators */
    int   win_sent   = 0;
    int   win_recv   = 0;
    int   win_retrans = 0;
    float win_rss_sum = 0.0f;
    int   window_num  = 0;

    /* Recovery time tracking */
    float jam_start_time    = -1.0f;  /* when CONFIRMED first seen  */
    float recover_time      = -1.0f;  /* when PDR recovered         */
    int   jam_active        = 0;      /* currently in jamming event */
    float recovery_time_sum = 0.0f;
    int   recovery_events   = 0;

    /* Totals across the whole run */
    float total_pdr_sum        = 0.0f;
    float total_throughput_sum = 0.0f;
    float total_rss_sum        = 0.0f;
    int   window_count         = 0;

    /* ── Main packet loop ─────────────────────────────────────── */
    for (int pkt = 0; pkt < TOTAL_PACKETS; pkt++) {
        float sim_time = (float)pkt * PACKET_INTERVAL;

        /* Step 1: base channel quality */
        float base_pdr = base_channel_pdr();
        float base_rss = base_channel_rss();

        /* Step 2: apply jammer */
        float eff_pdr, eff_rss;
        jammer_apply(&node->jammer,
                     base_pdr, base_rss,
                     1,          /* always transmitting */
                     &eff_pdr, &eff_rss);

        /* Step 3: packet outcome — stochastic decision */
        float roll = (float)rand() / (float)RAND_MAX;
        int received = (roll < eff_pdr) ? 1 : 0;

        /* Step 4: accumulate into window */
        win_sent++;
        if (received) {
            win_recv++;
        } else {
            win_retrans++;
        }
        win_rss_sum += eff_rss;

        /* Step 5: end of window — process and log */
        if (win_sent >= WINDOW_SIZE) {
            float win_pdr = (win_sent > 0)
                            ? (float)win_recv / (float)win_sent
                            : 0.0f;
            float win_rss = win_rss_sum / (float)win_sent;

            /* Build LinkMetrics snapshot for this window */
            LinkMetrics obs;
            obs.pdr           = win_pdr;
            obs.rss_dbm       = win_rss;
            obs.retrans_count = win_retrans;
            obs.packets_sent  = win_sent;
            obs.packets_recv  = win_recv;
            obs.timestamp     = (double)sim_time;

            /* Push to sliding window monitor */
            monitor_update(&node->monitor, obs);

            /* Run adaptive control (no-op for baseline node) */
            int action = adaptive_control(node);

            /* Track interference events and recovery time */
            InterferenceStatus status = monitor_classify(&node->monitor);
            if (status == INTERFERENCE_CONFIRMED && !jam_active) {
                jam_active      = 1;
                jam_start_time  = sim_time;
                result->interference_events++;
            }
            if (jam_active && win_pdr >= PDR_GOOD) {
                recover_time = sim_time;
                recovery_time_sum += (recover_time - jam_start_time);
                recovery_events++;
                jam_active = 0;
            }

            /* Accumulate run-level totals */
            float throughput = calc_throughput(win_pdr, node->packet_rate);
            total_pdr_sum        += win_pdr;
            total_throughput_sum += throughput;
            total_rss_sum        += win_rss;
            window_count++;

            result->total_packets_sent += win_sent;
            result->total_packets_recv += win_recv;
            result->total_retrans      += win_retrans;

            /* Write one CSV row per window */
            sim_write_csv_row(filename, window_num,
                              &obs, node->channel,
                              node->tx_power_dbm,
                              node->packet_rate,
                              action);
            window_num++;

            /* Reset window accumulators */
            win_sent    = 0;
            win_recv    = 0;
            win_retrans = 0;
            win_rss_sum = 0.0f;
        }
    }

    /* ── Compute final averages ───────────────────────────────── */
    if (window_count > 0) {
        result->avg_pdr        = total_pdr_sum        / (float)window_count;
        result->avg_throughput = total_throughput_sum / (float)window_count;
        result->avg_rss_dbm    = total_rss_sum        / (float)window_count;
    }

    result->recovery_time = (recovery_events > 0)
                            ? recovery_time_sum / (float)recovery_events
                            : 0.0f;
}

/* ─── sim_run_all ────────────────────────────────────────────────────
 *
 * Run all 8 scenarios and collect results.
 *
 * Scenarios:
 *   Jammer type × {Adaptive, Baseline} = 8 runs
 *
 *   We write per-run CSV files named after each scenario:
 *     adaptive_none.csv, baseline_none.csv,
 *     adaptive_constant.csv, baseline_constant.csv, ...
 *
 *   Then write the combined comparison table.
 */
void sim_run_all(void) {
    SimResult results[8];
    int       n = 0;

    JammerType types[4] = {
        JAMMER_NONE,
        JAMMER_CONSTANT,
        JAMMER_RANDOM,
        JAMMER_REACTIVE
    };

    float intensities[4] = { 0.0f, 1.0f, 1.0f, 1.0f };

    for (int t = 0; t < 4; t++) {
        JammerModel jammer;
        jammer_init(&jammer, types[t], intensities[t]);

        /* ── Adaptive run ─────────────────────────────────── */
        RadioNode adaptive_node;
        node_init(&adaptive_node, 1, jammer);

        /* Use scenario-specific CSV filename */
        char fname_a[64];
        snprintf(fname_a, sizeof(fname_a), "adaptive_%s.csv",
                 JAMMER_NAMES[types[t]]);

        printf("\nRunning: Adaptive vs %s jammer...\n",
               JAMMER_NAMES[types[t]]);

        /* Temporarily redirect output to scenario file */
        SimResult r_adaptive;
        snprintf(r_adaptive.scenario, sizeof(r_adaptive.scenario),
                 "Adaptive_%s", JAMMER_NAMES[types[t]]);

        /* Run with per-scenario CSV */
        sim_write_csv_header(fname_a);

        node_init(&adaptive_node, 1, jammer);
        sim_run_scenario(&adaptive_node, &r_adaptive, fname_a);
        snprintf(r_adaptive.scenario, sizeof(r_adaptive.scenario),
                 "Adaptive_%s", JAMMER_NAMES[types[t]]);

        sim_print_result(&r_adaptive);
        results[n++] = r_adaptive;

        /* ── Baseline run ─────────────────────────────────── */
        char fname_b[64];
        snprintf(fname_b, sizeof(fname_b), "baseline_%s.csv",
                 JAMMER_NAMES[types[t]]);

        printf("\nRunning: Baseline vs %s jammer...\n",
               JAMMER_NAMES[types[t]]);

        SimResult r_baseline;
        snprintf(r_baseline.scenario, sizeof(r_baseline.scenario),
                 "Baseline_%s", JAMMER_NAMES[types[t]]);

        sim_write_csv_header(fname_b);

        RadioNode baseline_node;
        node_init(&baseline_node, 0, jammer);
        sim_run_scenario(&baseline_node, &r_baseline, fname_b);
        snprintf(r_baseline.scenario, sizeof(r_baseline.scenario),
                 "Baseline_%s", JAMMER_NAMES[types[t]]);

        sim_print_result(&r_baseline);
        results[n++] = r_baseline;
    }

    /* Write the combined comparison CSV */
    sim_write_comparison(results, n);

    /* Print summary table to console */
    printf("\n");
    printf("%-30s  %6s  %10s  %8s  %8s\n",
           "Scenario", "PDR", "Throughput", "Retrans", "RecovTime");
    printf("%-30s  %6s  %10s  %8s  %8s\n",
       "------------------------------",
       "------", "----------", "-------", "--------");

    for (int i = 0; i < n; i++) {
        printf("%-30s  %6.4f  %10.4f  %8d  %8.2f\n",
               results[i].scenario,
               results[i].avg_pdr,
               results[i].avg_throughput,
               results[i].total_retrans,
               results[i].recovery_time);
    }
}

/*
 * sim_run_scenario — internal helper that runs a single scenario
 * and writes to a specific CSV file.
 * Separated from sim_run() so sim_run_all() can name files per scenario.
 */
void sim_run_scenario(RadioNode *node, SimResult *result,
                      const char *filename) {
    memset(result, 0, sizeof(SimResult));

    int   win_sent    = 0;
    int   win_recv    = 0;
    int   win_retrans = 0;
    float win_rss_sum = 0.0f;
    int   window_num  = 0;

    float jam_start_time    = -1.0f;
    int   jam_active        = 0;
    float recovery_time_sum = 0.0f;
    int   recovery_events   = 0;

    float total_pdr_sum        = 0.0f;
    float total_throughput_sum = 0.0f;
    float total_rss_sum        = 0.0f;
    int   window_count         = 0;

    for (int pkt = 0; pkt < TOTAL_PACKETS; pkt++) {
        float sim_time = (float)pkt * PACKET_INTERVAL;

        float base_pdr = base_channel_pdr();
        float base_rss = base_channel_rss();

        float eff_pdr, eff_rss;
        jammer_apply(&node->jammer, base_pdr, base_rss, 1,
                     &eff_pdr, &eff_rss);

        float roll = (float)rand() / (float)RAND_MAX;
        int received = (roll < eff_pdr) ? 1 : 0;

        win_sent++;
        if (received) { win_recv++; }
        else          { win_retrans++; }
        win_rss_sum += eff_rss;

        if (win_sent >= WINDOW_SIZE) {
            float win_pdr = (float)win_recv / (float)win_sent;
            float win_rss = win_rss_sum / (float)win_sent;

            LinkMetrics obs;
            obs.pdr           = win_pdr;
            obs.rss_dbm       = win_rss;
            obs.retrans_count = win_retrans;
            obs.packets_sent  = win_sent;
            obs.packets_recv  = win_recv;
            obs.timestamp     = (double)sim_time;

            monitor_update(&node->monitor, obs);
            int action = adaptive_control(node);

            InterferenceStatus status = monitor_classify(&node->monitor);
            if (status == INTERFERENCE_CONFIRMED && !jam_active) {
                jam_active = 1;
                jam_start_time = sim_time;
                result->interference_events++;
            }
            if (jam_active && win_pdr >= PDR_GOOD) {
                recovery_time_sum += (sim_time - jam_start_time);
                recovery_events++;
                jam_active = 0;
            }

            float throughput = calc_throughput(win_pdr, node->packet_rate);
            total_pdr_sum        += win_pdr;
            total_throughput_sum += throughput;
            total_rss_sum        += win_rss;
            window_count++;

            result->total_packets_sent += win_sent;
            result->total_packets_recv += win_recv;
            result->total_retrans      += win_retrans;

            sim_write_csv_row(filename, window_num, &obs,
                              node->channel, node->tx_power_dbm,
                              node->packet_rate, action);
            window_num++;

            win_sent    = 0;
            win_recv    = 0;
            win_retrans = 0;
            win_rss_sum = 0.0f;
        }
    }

    if (window_count > 0) {
        result->avg_pdr        = total_pdr_sum        / (float)window_count;
        result->avg_throughput = total_throughput_sum / (float)window_count;
        result->avg_rss_dbm    = total_rss_sum        / (float)window_count;
    }
    result->recovery_time = (recovery_events > 0)
                            ? recovery_time_sum / (float)recovery_events
                            : 0.0f;
}