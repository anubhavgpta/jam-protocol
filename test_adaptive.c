/*
 * test_adaptive.c — end-to-end test for adaptive.c
 *
 * Tests the full detect → classify → adapt cycle.
 * We feed known observations into the monitor, then call
 * adaptive_control() and verify the node's parameters changed
 * in exactly the way the protocol specifies.
 *
 * Add to CMakeLists.txt:
 *   add_executable(test_adaptive
 *       test_adaptive.c
 *       adaptive.c monitor.c jammer.c
 *   )
 */

#include <stdio.h>
#include <string.h>
#include "adaptive.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static const char *action_name(int a) {
    if (a == ACTION_NONE)                          return "NONE";
    if (a == ACTION_REDUCE_RATE)                   return "REDUCE_RATE";
    if (a == ACTION_SWITCH_CHANNEL)                return "SWITCH_CHANNEL";
    if (a == (ACTION_SWITCH_CHANNEL |
              ACTION_REDUCE_RATE    |
              ACTION_INCREASE_POWER))              return "FULL_MITIGATION";
    if (a == (ACTION_RESTORE_RATE |
              ACTION_RESTORE_POWER))               return "RESTORE";
    return "MIXED";
}

/* Push N identical observations into the node's monitor */
static void push_obs(RadioNode *node,
                     float pdr, float rss, int retrans) {
    LinkMetrics obs;
    obs.pdr           = pdr;
    obs.rss_dbm       = rss;
    obs.retrans_count = retrans;
    obs.packets_sent  = 10;
    obs.packets_recv  = (int)(pdr * 10.0f);
    obs.timestamp     = 0.0;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        monitor_update(&node->monitor, obs);
    }
}

static void print_node(const RadioNode *node) {
    printf("    channel=%d  rate=%d pkt/s  power=%.1f dBm\n",
           node->channel, node->packet_rate, node->tx_power_dbm);
}

static void check(const char *label, int cond) {
    printf("  %-45s %s\n", label, cond ? "PASS" : "FAIL");
}

/* ── Tests ───────────────────────────────────────────────────────── */

static void test_baseline_no_action(void) {
    printf("\nTest 1 — Baseline node never adapts\n");
    JammerModel j; jammer_init(&j, JAMMER_CONSTANT, 1.0f);
    RadioNode node; node_init(&node, 0, j);   /* is_adaptive = 0 */

    push_obs(&node, 0.30f, -48.0f, 8);       /* severe jamming */
    node.monitor.consecutive_bad = PERSIST_WINDOWS;

    int action = adaptive_control(&node);
    check("action == ACTION_NONE",  action == ACTION_NONE);
    check("channel unchanged (6)",  node.channel      == DEFAULT_CHANNEL);
    check("rate unchanged (10)",    node.packet_rate  == DEFAULT_PACKET_RATE);
    check("power unchanged (20)",   node.tx_power_dbm == DEFAULT_TX_POWER);
    print_node(&node);
}

static void test_healthy_link_no_action(void) {
    printf("\nTest 2 — Healthy link, no action taken\n");
    JammerModel j; jammer_init(&j, JAMMER_NONE, 0.0f);
    RadioNode node; node_init(&node, 1, j);

    push_obs(&node, 0.96f, -50.0f, 0);

    int action = adaptive_control(&node);
    printf("  Action: %s\n", action_name(action));
    check("action == NONE or RESTORE", action == ACTION_NONE ||
          action == (ACTION_RESTORE_RATE | ACTION_RESTORE_POWER));
    check("channel unchanged",  node.channel     == DEFAULT_CHANNEL);
    check("rate at default",    node.packet_rate == DEFAULT_PACKET_RATE);
    print_node(&node);
}

static void test_suspected_rate_only(void) {
    printf("\nTest 3 — Jamming suspected: rate reduced, no channel switch\n");
    JammerModel j; jammer_init(&j, JAMMER_RANDOM, 1.0f);
    RadioNode node; node_init(&node, 1, j);

    /* Establish baseline */
    push_obs(&node, 0.96f, -50.0f, 0);
    /* One bad window (consecutive_bad will become 1 after classify) */
    push_obs(&node, 0.55f, -48.0f, 5);
    node.monitor.consecutive_bad = 0; /* force SUSPECTED path */

    int action = adaptive_control(&node);
    printf("  Action: %s\n", action_name(action));
    check("action == REDUCE_RATE",      action == ACTION_REDUCE_RATE);
    check("rate decreased",             node.packet_rate < DEFAULT_PACKET_RATE);
    check("channel NOT switched",       node.channel == DEFAULT_CHANNEL);
    check("power NOT increased",        node.tx_power_dbm == DEFAULT_TX_POWER);
    print_node(&node);
}

static void test_confirmed_full_mitigation(void) {
    printf("\nTest 4 — Jamming confirmed: full mitigation applied\n");
    JammerModel j; jammer_init(&j, JAMMER_CONSTANT, 1.0f);
    RadioNode node; node_init(&node, 1, j);

    /* Establish baseline */
    push_obs(&node, 0.96f, -50.0f, 0);
    /* Multiple bad windows — RSS stable (jamming signature) */
    push_obs(&node, 0.35f, -41.0f, 8);
    /* Force consecutive_bad to trigger CONFIRMED */
    node.monitor.consecutive_bad = PERSIST_WINDOWS - 1;

    int old_channel = node.channel;
    int action = adaptive_control(&node);
    printf("  Action: %s\n", action_name(action));
    check("channel switched",           node.channel != old_channel);
    check("rate decreased",             node.packet_rate < DEFAULT_PACKET_RATE);
    check("power increased",            node.tx_power_dbm > DEFAULT_TX_POWER);
    check("action has SWITCH_CHANNEL",  action & ACTION_SWITCH_CHANNEL);
    check("action has REDUCE_RATE",     action & ACTION_REDUCE_RATE);
    check("action has INCREASE_POWER",  action & ACTION_INCREASE_POWER);
    print_node(&node);
}

static void test_fading_no_action(void) {
    printf("\nTest 5 — Fading detected: no action taken\n");
    JammerModel j; jammer_init(&j, JAMMER_NONE, 0.0f);
    RadioNode node; node_init(&node, 1, j);

    /* Good baseline */
    push_obs(&node, 0.96f, -50.0f, 0);
    /* Fading: both PDR and RSS drop together */
    push_obs(&node, 0.60f, -74.0f, 2);

    int old_channel = node.channel;
    int action = adaptive_control(&node);
    printf("  Action: %s\n", action_name(action));
    check("action == NONE (fading, not jamming)", action == ACTION_NONE);
    check("channel NOT switched",       node.channel     == old_channel);
    check("rate NOT changed",           node.packet_rate == DEFAULT_PACKET_RATE);
    print_node(&node);
}

static void test_recovery_restores_params(void) {
    printf("\nTest 6 — Recovery: parameters restored after jamming ends\n");
    JammerModel j; jammer_init(&j, JAMMER_CONSTANT, 1.0f);
    RadioNode node; node_init(&node, 1, j);

    /* Simulate: jamming already happened, params were adjusted */
    node.packet_rate  = MIN_PACKET_RATE;       /* rate was reduced */
    node.tx_power_dbm = MAX_TX_POWER;          /* power was raised  */

    /* Now jamming stopped — push healthy observations */
    push_obs(&node, 0.96f, -50.0f, 0);

    int action = adaptive_control(&node);
    printf("  Action: %s\n", action_name(action));
    check("restore action fired",       action == (ACTION_RESTORE_RATE |
                                                    ACTION_RESTORE_POWER));
    check("rate increased toward default",
          node.packet_rate > MIN_PACKET_RATE);
    check("power decreased toward default",
          node.tx_power_dbm < MAX_TX_POWER);
    print_node(&node);
}

static void test_channel_roundrobin(void) {
    printf("\nTest 7 — Channel round-robin wraps correctly\n");
    JammerModel j; jammer_init(&j, JAMMER_NONE, 0.0f);
    RadioNode node; node_init(&node, 1, j);

    printf("  Switching through all channels:\n  ");
    node.channel = NUM_CHANNELS; /* start at last channel */
    for (int i = 0; i < NUM_CHANNELS + 2; i++) {
        int prev = node.channel;
        int next = adaptive_switch_channel(&node);
        printf("%d→%d  ", prev, next);
    }
    printf("\n");
    check("never exceeds NUM_CHANNELS",  node.channel <= NUM_CHANNELS);
    check("never goes below 1",          node.channel >= 1);
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void) {
    printf("==============================================\n");
    printf("  Adaptive Control Engine Test\n");
    printf("==============================================\n");
    printf("Defaults: channel=%d rate=%d power=%.0f dBm\n",
           DEFAULT_CHANNEL, DEFAULT_PACKET_RATE, DEFAULT_TX_POWER);

    test_baseline_no_action();
    test_healthy_link_no_action();
    test_suspected_rate_only();
    test_confirmed_full_mitigation();
    test_fading_no_action();
    test_recovery_restores_params();
    test_channel_roundrobin();

    printf("\n==============================================\n");
    printf("  All tests complete.\n");
    printf("==============================================\n");
    return 0;
}