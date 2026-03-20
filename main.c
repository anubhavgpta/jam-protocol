#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "jammer.h"
#include "monitor.h"
#include "adaptive.h"
#include "simulation.h"

int main(void) {
    /* Seed the RNG — random jammer behaviour depends on this */
    srand((unsigned int)time(NULL));

    printf("===========================================\n");
    printf("  Lightweight Jam-Resistant Adaptive Protocol\n");
    printf("  Simulation Engine - Anubhav & Kaashyap\n");
    printf("===========================================\n\n");

    printf("Configuration:\n");
    printf("  Total packets per run : %d\n",  TOTAL_PACKETS);
    printf("  Sliding window size   : %d\n",  WINDOW_SIZE);
    printf("  PDR threshold         : %.2f\n", PDR_THRESHOLD);
    printf("  Channels available    : %d\n",  NUM_CHANNELS);
    printf("  Default TX power      : %.1f dBm\n", DEFAULT_TX_POWER);
    printf("\n");

    /*
     * sim_run_all() will be implemented in Step 5.
     * It runs all 8 scenarios (4 jammers × adaptive + baseline)
     * and writes the output CSVs.
     */
    sim_run_all();

    printf("\nAll scenarios complete. Output files written:\n");
    printf("  %s\n", OUTPUT_CSV_ADAPTIVE);
    printf("  %s\n", OUTPUT_CSV_BASELINE);
    printf("  %s\n", OUTPUT_CSV_COMPARISON);

    return 0;
}