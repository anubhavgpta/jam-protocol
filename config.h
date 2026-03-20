#ifndef CONFIG_H
#define CONFIG_H

/* ─── Simulation parameters ─────────────────────────── */
#define SIM_DURATION        100       /* Total simulation time (seconds)     */
#define PACKET_INTERVAL     0.1       /* Time between packet transmissions   */
#define TOTAL_PACKETS       1000      /* Packets per simulation run          */
#define WINDOW_SIZE         20        /* Sliding window size (packets)       */

/* ─── Channel parameters ────────────────────────────── */
#define NUM_CHANNELS        12        /* Available frequency channels        */
#define DEFAULT_CHANNEL     6         /* Starting channel                    */
#define MIN_CHANNEL         1
#define MAX_CHANNEL         12

/* ─── Transmission power (dBm) ──────────────────────── */
#define DEFAULT_TX_POWER    20.0f     /* Default transmit power (dBm)        */
#define MIN_TX_POWER        5.0f      /* Minimum allowed power               */
#define MAX_TX_POWER        30.0f     /* Maximum allowed power               */
#define POWER_STEP_UP       3.0f      /* Power increase per adaptation step  */
#define POWER_STEP_DOWN     2.0f      /* Power decrease (restore efficiency) */

/* ─── Packet rate (packets/second) ──────────────────── */
#define DEFAULT_PACKET_RATE 10        /* Default packets per second          */
#define MIN_PACKET_RATE     2         /* Minimum rate under heavy jamming    */
#define MAX_PACKET_RATE     20        /* Maximum rate (clear channel)        */
#define RATE_STEP_DOWN      2         /* Rate decrease per adaptation step   */
#define RATE_STEP_UP        1         /* Rate increase during recovery       */

/* ─── RSS thresholds (dBm) ──────────────────────────── */
#define RSS_BASELINE       -50.0f     /* Expected RSS on a clean channel     */
#define RSS_MIN_THRESHOLD  -85.0f     /* Below this = link nearly unusable   */
#define RSS_NOISE_FLOOR    -95.0f     /* Thermal noise floor                 */

/* ─── Interference detection thresholds ─────────────── */
#define PDR_THRESHOLD       0.75f     /* PDR below this triggers mitigation  */
#define PDR_GOOD            0.90f     /* PDR above this = link is healthy    */
#define RETRANS_THRESHOLD   5         /* Retransmit count triggers action    */
#define PERSIST_WINDOWS     3         /* Consecutive bad windows = jamming   */
#define RSS_DROP_TOLERANCE  10.0f     /* Max RSS drop before fading assumed  */

/* ─── Jammer parameters ──────────────────────────────── */
#define JAMMER_CONSTANT_PDR_DROP   0.60f  /* Constant jammer PDR reduction   */
#define JAMMER_RANDOM_PROB         0.40f  /* Probability random jammer fires  */
#define JAMMER_RANDOM_PDR_DROP     0.70f  /* PDR when random jammer fires     */
#define JAMMER_REACTIVE_THRESH     0.80f  /* Reactive jammer triggers at PDR  */
#define JAMMER_REACTIVE_PDR_DROP   0.50f  /* Reactive jammer PDR reduction    */
#define JAMMER_RSS_RAISE           15.0f  /* RSS apparent rise due to noise   */

/* ─── Output file paths ──────────────────────────────── */
#define OUTPUT_CSV_ADAPTIVE    "results_adaptive.csv"
#define OUTPUT_CSV_BASELINE    "results_baseline.csv"
#define OUTPUT_CSV_COMPARISON  "results_comparison.csv"

#endif /* CONFIG_H */