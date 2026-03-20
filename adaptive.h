#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include "monitor.h"
#include "jammer.h"

/*
 * AdaptiveAction — what the Decision Engine decided to do.
 * Multiple flags can be set at once for combined responses.
 *
 * Example: a reactive jammer needs BOTH channel switch AND rate
 * reduction simultaneously, so the engine sets both flags.
 */
typedef enum {
    ACTION_NONE          = 0x00,  /* No action needed                     */
    ACTION_SWITCH_CHANNEL= 0x01,  /* Change to a different frequency      */
    ACTION_REDUCE_RATE   = 0x02,  /* Lower packet transmission rate       */
    ACTION_INCREASE_POWER= 0x04,  /* Raise transmit power                 */
    ACTION_RESTORE_RATE  = 0x08,  /* Recover rate after interference ends */
    ACTION_RESTORE_POWER = 0x10   /* Restore power to default             */
} AdaptiveAction;

/*
 * RadioNode — the complete state of one simulated radio.
 *
 * This struct is passed to every module. The adaptive engine
 * reads the metrics and writes back the updated parameters
 * (channel, tx_power_dbm, packet_rate).
 */
typedef struct {
    int   channel;          /* Current operating channel (1–12)         */
    float tx_power_dbm;     /* Current transmit power (dBm)             */
    int   packet_rate;      /* Current packet rate (packets/sec)        */
    float rss_dbm;          /* Last observed RSS (dBm)                  */
    float pdr;              /* Last computed PDR                        */
    int   retrans_count;    /* Running retransmission count             */
    int   is_adaptive;      /* 1 = adaptive protocol, 0 = baseline      */
    MonitorState monitor;   /* Embedded sliding-window monitor          */
    JammerModel  jammer;    /* Jammer affecting this node               */
} RadioNode;

/* ─── Public interface ──────────────────────────────── */

/* node_init — initialize a RadioNode with default parameters */
void node_init(RadioNode *node, int is_adaptive, JammerModel jammer);

/*
 * adaptive_control — the main decision loop, called once per window.
 *
 * Steps:
 *   1. Calls monitor_classify() to get the current interference status
 *   2. Selects the appropriate set of AdaptiveActions
 *   3. Applies those actions to the node's parameters
 *   4. Returns a bitmask of actions taken (useful for logging)
 *
 * If is_adaptive == 0, this function returns ACTION_NONE immediately
 * (baseline node makes no adjustments).
 */
int adaptive_control(RadioNode *node);

/*
 * adaptive_switch_channel — pick the next best channel.
 * Simple round-robin avoiding the current channel.
 * Returns the new channel number.
 */
int adaptive_switch_channel(RadioNode *node);

/*
 * adaptive_reduce_rate — step down the packet rate.
 * Clamps to MIN_PACKET_RATE.
 */
void adaptive_reduce_rate(RadioNode *node);

/*
 * adaptive_increase_power — step up transmit power.
 * Clamps to MAX_TX_POWER.
 */
void adaptive_increase_power(RadioNode *node);

/*
 * adaptive_restore — gradually restore rate and power
 * when interference subsides. Called when PDR recovers.
 */
void adaptive_restore(RadioNode *node);

#endif /* ADAPTIVE_H */