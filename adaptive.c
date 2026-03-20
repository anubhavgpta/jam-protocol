/*
 * adaptive.c — Decision Engine and Parameter Controller
 *
 * This module sits between monitor.c (what is happening?) and the
 * PHY/MAC layer parameters (what should we do about it?).
 *
 * The core function is adaptive_control(), called once per window.
 * It reads the InterferenceStatus from monitor_classify() and applies
 * a graduated response:
 *
 *   NONE      → restore parameters if they were previously adjusted
 *   FADING    → do nothing (fading self-resolves; acting wastes power)
 *   SUSPECTED → reduce packet rate only (cheap, reversible)
 *   CONFIRMED → switch channel + reduce rate + increase power
 *
 * All parameter changes are clamped to safe ranges defined in config.h.
 * The bitmask return value lets simulation.c log exactly what happened.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adaptive.h"

/* ─── node_init ──────────────────────────────────────────────────────
 *
 * Set a RadioNode to its starting state before simulation begins.
 * All parameters start at their defaults from config.h.
 * The monitor is initialised (zeroed) and the jammer is embedded.
 *
 * is_adaptive = 1 → protocol enabled (adaptive_control does work)
 * is_adaptive = 0 → baseline node (adaptive_control returns immediately)
 */
void node_init(RadioNode *node, int is_adaptive, JammerModel jammer) {
    node->channel       = DEFAULT_CHANNEL;
    node->tx_power_dbm  = DEFAULT_TX_POWER;
    node->packet_rate   = DEFAULT_PACKET_RATE;
    node->rss_dbm       = RSS_BASELINE;
    node->pdr           = 1.0f;
    node->retrans_count = 0;
    node->is_adaptive   = is_adaptive;
    node->jammer        = jammer;
    monitor_init(&node->monitor);
}

/* ─── adaptive_switch_channel ────────────────────────────────────────
 *
 * Move to the next channel in a round-robin sequence.
 *
 * Why round-robin and not "find the best channel"?
 * Finding the best channel requires scanning all channels, which takes
 * time and energy — exactly what we are trying to minimise. Round-robin
 * is O(1) and statistically equivalent when channels are independent.
 *
 * The +1 offset before the modulo ensures we never stay on the same
 * channel: if current = 6, next = ((6-1+1) % 12) + 1 = 7.
 *
 * Channels are numbered 1..NUM_CHANNELS (not 0-based) because that
 * matches real-world channel numbering (e.g. 802.11 channels 1–12).
 */
int adaptive_switch_channel(RadioNode *node) {
    /* Map to 0-based, advance by 1, wrap, map back to 1-based */
    node->channel = (node->channel % NUM_CHANNELS) + 1;
    return node->channel;
}

/* ─── adaptive_reduce_rate ───────────────────────────────────────────
 *
 * Step down the packet transmission rate by RATE_STEP_DOWN.
 *
 * Why does reducing rate help?
 *
 * Against random jammers: lower rate = fewer packets in any given
 * time window = fewer overlap events with jammer bursts. Effective
 * PDR improves even though the jammer is still active.
 *
 * Against reactive jammers: the jammer triggers on detected RF energy.
 * Longer gaps between packets give the jammer less to react to.
 * Reducing rate lengthens those gaps.
 *
 * Against constant jammers: minimal benefit (jammer doesn't care about
 * our rate), but the reduced rate lowers our retransmission overhead.
 *
 * Clamped to MIN_PACKET_RATE so we never drop below a usable link.
 */
void adaptive_reduce_rate(RadioNode *node) {
    node->packet_rate -= RATE_STEP_DOWN;
    if (node->packet_rate < MIN_PACKET_RATE) {
        node->packet_rate = MIN_PACKET_RATE;
    }
}

/* ─── adaptive_increase_power ────────────────────────────────────────
 *
 * Step up transmit power by POWER_STEP_UP dBm.
 *
 * Why does increasing power help?
 *
 * The signal-to-interference-plus-noise ratio (SINR) at the receiver:
 *
 *   SINR = P_signal / (P_jammer + P_noise)
 *
 * Increasing P_signal directly improves SINR, which improves the
 * probability of correct packet decoding (PDR). This is most effective
 * against constant jammers where the interference power is fixed —
 * we can out-power them if our transmitter allows it.
 *
 * Less effective against reactive jammers: they increase their own
 * power in response (an arms race). Channel switching breaks the
 * reactive jammer's lock faster than out-powering it.
 *
 * Clamped to MAX_TX_POWER — exceeding this causes regulatory
 * violations in real hardware and inter-channel interference.
 */
void adaptive_increase_power(RadioNode *node) {
    node->tx_power_dbm += POWER_STEP_UP;
    if (node->tx_power_dbm > MAX_TX_POWER) {
        node->tx_power_dbm = MAX_TX_POWER;
    }
}

/* ─── adaptive_restore ───────────────────────────────────────────────
 *
 * Gradually restore rate and power toward their defaults.
 * Called when PDR has recovered above PDR_GOOD.
 *
 * Why gradually and not instantly?
 * Snapping back to maximum rate immediately after recovery risks
 * re-triggering the jammer (especially reactive types). A slow
 * ramp-up gives the link time to confirm stability before we
 * increase our radio footprint again.
 *
 * Rate recovers faster than power (RATE_STEP_UP=1 vs POWER_STEP_DOWN=2)
 * because rate affects congestion immediately, while power affects
 * the RF environment more gradually.
 */
void adaptive_restore(RadioNode *node) {
    /* Restore packet rate toward default */
    if (node->packet_rate < DEFAULT_PACKET_RATE) {
        node->packet_rate += RATE_STEP_UP;
        if (node->packet_rate > DEFAULT_PACKET_RATE) {
            node->packet_rate = DEFAULT_PACKET_RATE;
        }
    }

    /* Restore transmit power toward default */
    if (node->tx_power_dbm > DEFAULT_TX_POWER) {
        node->tx_power_dbm -= POWER_STEP_DOWN;
        if (node->tx_power_dbm < DEFAULT_TX_POWER) {
            node->tx_power_dbm = DEFAULT_TX_POWER;
        }
    }
}

/* ─── adaptive_control ───────────────────────────────────────────────
 *
 * Main decision loop — called once per sliding window.
 *
 * Flow:
 *   1. If baseline node (is_adaptive=0): return ACTION_NONE immediately.
 *      The baseline never adjusts — that's the point of the comparison.
 *
 *   2. Call monitor_classify() to get the current interference status.
 *
 *   3. Select actions based on status:
 *
 *      NONE:
 *        Link is healthy. If parameters were previously adjusted,
 *        call adaptive_restore() to ramp them back toward defaults.
 *        Return ACTION_RESTORE_RATE | ACTION_RESTORE_POWER if restoring,
 *        or ACTION_NONE if already at defaults.
 *
 *      FADING:
 *        PDR and RSS both dropped. This is path loss / multipath,
 *        not jamming. No action taken. Attempting channel switch
 *        during fading would waste a switch on a problem that
 *        self-resolves and might put us on a worse channel.
 *        Return ACTION_NONE.
 *
 *      SUSPECTED:
 *        First or second bad window. PDR dropped, RSS stable.
 *        Apply only rate reduction — it's cheap and reversible.
 *        Don't switch channels yet (might be a false alarm).
 *        Return ACTION_REDUCE_RATE.
 *
 *      CONFIRMED:
 *        Three or more consecutive bad windows, RSS stable.
 *        Apply all three mitigations simultaneously:
 *          - Switch channel (escapes narrowband and reactive jammers)
 *          - Reduce rate   (reduces collision probability)
 *          - Increase power (improves SINR against constant jammers)
 *        Return ACTION_SWITCH_CHANNEL | ACTION_REDUCE_RATE |
 *               ACTION_INCREASE_POWER.
 *
 * Returns: bitmask of AdaptiveAction values (defined in adaptive.h).
 *          simulation.c uses this to log what happened each window.
 */
int adaptive_control(RadioNode *node) {
    /* Baseline node: never adapt */
    if (!node->is_adaptive) {
        return ACTION_NONE;
    }

    InterferenceStatus status = monitor_classify(&node->monitor);
    int actions = ACTION_NONE;

    switch (status) {

    case INTERFERENCE_NONE:
        /*
         * Link is healthy. Restore parameters if they were
         * previously reduced. This is the recovery phase.
         */
        if (node->packet_rate < DEFAULT_PACKET_RATE ||
            node->tx_power_dbm > DEFAULT_TX_POWER) {
            adaptive_restore(node);
            actions = ACTION_RESTORE_RATE | ACTION_RESTORE_POWER;
        }
        break;

    case INTERFERENCE_FADING:
        /*
         * Fading detected: PDR and RSS fell together.
         * No action — fading is transient and channel-switching
         * would not help (the new channel may fade too).
         */
        actions = ACTION_NONE;
        break;

    case INTERFERENCE_SUSPECTED:
        /*
         * First sign of potential jamming.
         * Reduce rate only — wait for confirmation before
         * committing to a channel switch.
         */
        adaptive_reduce_rate(node);
        actions = ACTION_REDUCE_RATE;
        break;

    case INTERFERENCE_CONFIRMED:
        /*
         * Jamming confirmed across multiple windows.
         * Apply full mitigation: channel + rate + power.
         * Order matters: switch channel first (escape the jammer),
         * then adjust rate and power for the new channel.
         */
        adaptive_switch_channel(node);
        adaptive_reduce_rate(node);
        adaptive_increase_power(node);
        actions = ACTION_SWITCH_CHANNEL |
                  ACTION_REDUCE_RATE    |
                  ACTION_INCREASE_POWER;
        break;
    }

    return actions;
}