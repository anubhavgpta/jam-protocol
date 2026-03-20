#ifndef SIMULATION_H
#define SIMULATION_H

#include "adaptive.h"

/*
 * SimResult — collected performance metrics for one complete run.
 *
 * One SimResult is produced for each combination of:
 *   (jammer type) × (adaptive | baseline)
 *
 * That gives us 8 runs total (4 jammer types × 2 protocol modes),
 * which is exactly what we need for the comparison table in the paper.
 */
typedef struct {
    float avg_pdr;              /* Average PDR over the full run           */
    float avg_throughput;       /* Avg throughput (packets/sec delivered)  */
    float avg_rss_dbm;          /* Average RSS over the full run           */
    int   total_retrans;        /* Total retransmissions across all windows */
    float recovery_time;        /* Seconds from jam-start to PDR recovery  */
    int   interference_events;  /* Number of times interference was detected*/
    int   total_packets_sent;   /* Total packets attempted                  */
    int   total_packets_recv;   /* Total packets successfully delivered     */
    char  scenario[64];         /* Label, e.g. "Adaptive_Reactive"          */
} SimResult;

/* ─── Public interface ──────────────────────────────── */

/*
 * sim_run — execute one complete simulation run.
 *
 * Parameters:
 *   node   — fully initialised RadioNode (jammer + protocol type set)
 *   result — output: filled with performance metrics at end of run
 *
 * This function:
 *   1. Loops for TOTAL_PACKETS iterations
 *   2. Each iteration: applies jammer, measures link quality,
 *      pushes to monitor window, calls adaptive_control if enabled
 *   3. Logs per-window data to the node's CSV file
 */
void sim_run(RadioNode *node, SimResult *result);

/*
 * sim_run_all — run all 8 scenarios and collect results.
 * Writes per-window CSVs and the final comparison CSV.
 */
void sim_run_all(void);

/*
 * sim_write_csv_header — write the column header row to a CSV file.
 * Returns 0 on success, -1 on failure.
 */
int sim_write_csv_header(const char *filename);

/*
 * sim_write_csv_row — append one window's metrics to a CSV file.
 */
void sim_write_csv_row(const char *filename, int window_num,
                       const LinkMetrics *m, int channel,
                       float tx_power, int packet_rate,
                       int action_taken);

/*
 * sim_write_comparison — write the summary comparison table CSV.
 * Takes an array of SimResult and the count.
 */
void sim_write_comparison(const SimResult *results, int count);

/*
 * sim_print_result — print a SimResult to stdout in a readable format.
 */
void sim_print_result(const SimResult *r);

/*
 * sim_run_scenario — internal helper: run one scenario into a named CSV.
 * Called by sim_run_all() for each of the 8 runs.
 */
void sim_run_scenario(RadioNode *node, SimResult *result,
                      const char *filename);

#endif /* SIMULATION_H */