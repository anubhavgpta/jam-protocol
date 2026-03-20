#ifndef JAMMER_H
#define JAMMER_H

#include "config.h"

/*
 * Three jammer types modelled in this simulation:
 *
 *  JAMMER_CONSTANT  — Always active. Floods the channel continuously.
 *                     Effect: PDR drops steadily, RSS appears elevated
 *                     because the jammer signal overlaps with our signal.
 *
 *  JAMMER_RANDOM    — Fires at random intervals (Bernoulli process).
 *                     Effect: Sporadic PDR drops. Hard to distinguish
 *                     from channel fading using a single snapshot —
 *                     requires the sliding window to catch the pattern.
 *
 *  JAMMER_REACTIVE  — Listens first, then jams when it detects a
 *                     transmission. Effect: PDR crashes instantly when
 *                     we transmit, then recovers during silence. This
 *                     is the most dangerous type in practice.
 *
 *  JAMMER_NONE      — No interference. Used for baseline runs.
 */
typedef enum {
    JAMMER_NONE     = 0,
    JAMMER_CONSTANT = 1,
    JAMMER_RANDOM   = 2,
    JAMMER_REACTIVE = 3
} JammerType;

/* Declared here, defined once in jammer.c */
extern const char *JAMMER_NAMES[];

/*
 * JammerModel — carries the type and its intensity factor.
 * intensity: 0.0 = no effect, 1.0 = maximum jamming power.
 * active:    runtime flag, toggled by reactive jammer logic.
 */
typedef struct {
    JammerType type;
    float      intensity;   /* 0.0 – 1.0 */
    int        active;      /* 1 if currently jamming, 0 if not */
} JammerModel;

/* ─── Public interface ─────────────────────────────── */

/*
 * jammer_init — set up a jammer with type and intensity.
 * Call this once before the simulation loop.
 */
void jammer_init(JammerModel *j, JammerType type, float intensity);

/*
 * jammer_apply — compute the PDR and RSS seen by the receiver
 * given the current jammer state. 
 *
 * Parameters:
 *   j              — the jammer model
 *   base_pdr       — PDR expected on a clean channel (0.0–1.0)
 *   base_rss       — RSS expected on a clean channel (dBm)
 *   transmitting   — 1 if node is currently transmitting (for reactive)
 *   out_pdr        — output: effective PDR after jamming
 *   out_rss        — output: effective RSS after jamming
 */
void jammer_apply(JammerModel *j,
                  float base_pdr, float base_rss,
                  int transmitting,
                  float *out_pdr, float *out_rss);

/*
 * jammer_reset — reset any stateful jammer fields.
 * Call between simulation runs.
 */
void jammer_reset(JammerModel *j);

#endif /* JAMMER_H */