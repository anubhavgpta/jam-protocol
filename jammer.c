/*
 * jammer.c — Interference and Jammer Models
 *
 * Implements three jammer types:
 *   1. Constant  — always-on broadband noise
 *   2. Random    — fires at random intervals (Bernoulli process)
 *   3. Reactive  — listens first, jams only when transmission detected
 *
 * Each model modifies two observables: PDR and RSS.
 * The RSS behaviour is the key: jammers raise RSS (they add power),
 * while channel fading lowers RSS. This asymmetry is how monitor.c
 * distinguishes the two conditions.
 */

#include <stdlib.h>   /* rand(), RAND_MAX */
#include <stdio.h>    /* printf (used in test harness) */
#include "jammer.h"

/* Defined once here — declared extern in jammer.h */
const char *JAMMER_NAMES[] = {
    "None", "Constant", "Random", "Reactive"
};

/* ─── Internal helpers ───────────────────────────────────────────────── */

/*
 * rand_float — returns a uniform random float in [0.0, 1.0].
 * Used for probabilistic jammer decisions.
 *
 * Why not just use rand()? rand() returns an int (0..RAND_MAX).
 * Dividing by RAND_MAX+1.0 maps it into [0, 1). The cast to float
 * is intentional — we don't need double precision for this.
 */
static float rand_float(void) {
    return (float)rand() / ((float)RAND_MAX + 1.0f);
}

/*
 * clamp_pdr — keep PDR within physically valid bounds [0.0, 1.0].
 * A PDR above 1.0 or below 0.0 is meaningless.
 */
static float clamp_pdr(float pdr) {
    if (pdr < 0.0f) return 0.0f;
    if (pdr > 1.0f) return 1.0f;
    return pdr;
}

/*
 * clamp_rss — keep RSS above the thermal noise floor.
 * Signals below RSS_NOISE_FLOOR cannot be detected by any receiver.
 */
static float clamp_rss(float rss) {
    if (rss < RSS_NOISE_FLOOR) return RSS_NOISE_FLOOR;
    return rss;
}

/* ─── Public functions ───────────────────────────────────────────────── */

void jammer_init(JammerModel *j, JammerType type, float intensity) {
    j->type      = type;
    j->intensity = intensity;
    j->active    = 0;   /* not currently jamming at start */
}

void jammer_reset(JammerModel *j) {
    j->active = 0;
}

/*
 * jammer_apply — the core of this module.
 *
 * For each jammer type, we compute:
 *   1. Whether the jammer is currently firing
 *   2. The resulting PDR degradation
 *   3. The resulting RSS change
 *
 * Parameters:
 *   j            — jammer model (may update j->active for reactive type)
 *   base_pdr     — PDR we would have on a perfectly clean channel
 *   base_rss     — RSS we would have on a perfectly clean channel
 *   transmitting — 1 if the node is mid-transmission (for reactive)
 *   out_pdr      — write the resulting PDR here
 *   out_rss      — write the resulting RSS here
 */
void jammer_apply(JammerModel *j,
                  float base_pdr, float base_rss,
                  int transmitting,
                  float *out_pdr, float *out_rss)
{
    float pdr = base_pdr;
    float rss = base_rss;

    switch (j->type) {

    /* ── JAMMER_NONE ───────────────────────────────────────────────
     * No interference. Pass through the base values unchanged.
     * Used for the no-jammer baseline run.
     */
    case JAMMER_NONE:
        j->active = 0;
        break;

    /* ── JAMMER_CONSTANT ───────────────────────────────────────────
     * Always broadcasting noise on the channel.
     *
     * PDR model:
     *   The jammer continuously raises the noise floor. Every packet
     *   competes against a fixed interference power. We model this as
     *   a linear reduction scaled by intensity:
     *
     *     effective_pdr = base_pdr × (1 − intensity × DROP_FACTOR)
     *
     *   With default intensity=1.0 and DROP_FACTOR=0.60:
     *     effective_pdr = 0.98 × (1 − 0.60) = 0.392
     *   This matches observed PDR under a constant barrage jammer
     *   (Goldsmith, Ch. 4: SNR-to-PDR mapping for BPSK).
     *
     * RSS model:
     *   The jammer adds RF power to the channel. Our receiver sees
     *   the combined power of our signal + jammer noise. RSS rises
     *   by a fraction of the jammer's intensity scaled by RSS_RAISE.
     *   This is the key signature that separates jamming from fading.
     */
    case JAMMER_CONSTANT:
        j->active = 1;
        pdr = base_pdr * (1.0f - j->intensity * JAMMER_CONSTANT_PDR_DROP);
        rss = base_rss + (j->intensity * JAMMER_RSS_RAISE * 0.6f);
        break;

    /* ── JAMMER_RANDOM ─────────────────────────────────────────────
     * Fires with probability JAMMER_RANDOM_PROB on each packet.
     * Models a jammer that has limited power budget and duty-cycles.
     *
     * PDR model:
     *   Each packet independently: roll a random number.
     *   If roll < fire_probability → jammer is active this packet.
     *   When active, PDR degrades sharply (similar to constant but
     *   shorter duration). When not active, channel is clean.
     *
     *   Effective (average) PDR over many windows:
     *     E[PDR] = (1−p)×base_pdr + p×degraded_pdr
     *   where p = JAMMER_RANDOM_PROB = 0.40
     *
     * RSS model:
     *   RSS rises only during active jamming windows.
     *   During quiet windows, RSS returns to baseline.
     *   This intermittency is why a single-sample RSS check
     *   fails — you need the sliding window average.
     */
    case JAMMER_RANDOM: {
        float roll = rand_float();
        if (roll < JAMMER_RANDOM_PROB * j->intensity) {
            /* Jammer fired this packet window */
            j->active = 1;
            pdr = base_pdr * (1.0f - JAMMER_RANDOM_PDR_DROP * j->intensity);
            rss = base_rss + (j->intensity * JAMMER_RSS_RAISE * 0.4f);
        } else {
            /* Quiet window — clean channel */
            j->active = 0;
            pdr = base_pdr;
            rss = base_rss;
        }
        break;
    }

    /* ── JAMMER_REACTIVE ───────────────────────────────────────────
     * The most sophisticated and dangerous type.
     * The jammer listens for RF energy. When it detects a
     * transmission, it immediately begins jamming at full power.
     * When the channel goes quiet, it stops (saves its own power).
     *
     * PDR model:
     *   If transmitting == 1: jammer activates, PDR crashes severely.
     *   If transmitting == 0: jammer is silent, PDR recovers.
     *
     *   The severe drop (REACTIVE_PDR_DROP = 0.50) reflects that
     *   the reactive jammer can achieve nearly perfect interference
     *   because it synchronises with the victim's transmissions.
     *
     * RSS model:
     *   RSS spikes sharply when jammer fires (full power burst),
     *   then drops back when silent. The spike is larger than the
     *   constant jammer because reactive jammers typically use
     *   higher instantaneous power for short bursts.
     *
     * Channel switching effectiveness:
     *   A reactive jammer on channel C cannot follow a channel switch
     *   immediately — it needs to re-scan. This is WHY channel
     *   switching is effective against reactive jammers specifically.
     *   Our adaptive_switch_channel() exploits this exact weakness.
     */
    case JAMMER_REACTIVE:
        if (transmitting) {
            j->active = 1;
            pdr = base_pdr * (1.0f - j->intensity * JAMMER_REACTIVE_PDR_DROP);
            rss = base_rss + (j->intensity * JAMMER_RSS_RAISE);
        } else {
            j->active = 0;
            pdr = base_pdr;
            rss = base_rss;
        }
        break;
    }

    /* Write clamped results to output pointers */
    *out_pdr = clamp_pdr(pdr);
    *out_rss = clamp_rss(rss);
}