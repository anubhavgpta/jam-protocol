# Lightweight Jam-Resistant Adaptive Communication Protocol

> A software simulation of an adaptive wireless communication protocol
> that detects and mitigates jamming attacks using lightweight statistical
> metrics — no machine learning, no heavy signal processing.
>
> **Authors:** Anubhav Gupta & Kaashyap Sai Varma
> Electronics and Communication Engineering, Manipal Institute of Technology

---

## Overview

Tactical and mission-critical wireless networks are highly vulnerable to
intentional interference (jamming). Existing mitigation strategies rely on
complex ML algorithms or heavy signal processing — unsuitable for
resource-constrained environments.

This project implements a **threshold-based adaptive protocol** that:
- Monitors link quality using a sliding window (PDR, RSS, retransmission count)
- Distinguishes **jamming** from **channel fading** using RSS divergence
- Responds with graduated mitigation: channel switching, rate reduction, power adjustment
- Runs entirely in software with O(1) per-packet overhead

---

## Jammer Models

Three interference models are simulated:

| Jammer | Behaviour | Key Effect |
|---|---|---|
| **Constant** | Always-on broadband noise | PDR drops to ~0.39, RSS rises |
| **Random** | Fires with 40% probability per packet | Sporadic PDR drops, intermittent RSS rise |
| **Reactive** | Jams only when transmission detected | Immediate PDR crash on every transmission |

Each jammer is tested against both the **adaptive protocol** and a **non-adaptive baseline** — 8 scenarios total.

---

## Protocol Architecture

```
main.c
  └── simulation.c          ← runs all 8 scenarios, writes CSV output
        ├── jammer.c         ← constant / random / reactive jammer models
        ├── monitor.c        ← sliding window, PDR/RSS/retrans metrics
        └── adaptive.c       ← decision engine + parameter controller
              ├── PHY layer  ← channel switching, power adjustment
              └── MAC layer  ← packet rate adaptation
```

### Detection Algorithm (monitor_classify)

```
IF window PDR >= 0.75:
    → NONE (healthy)
ELSE IF RSS dropped > 10 dBm from baseline:
    → FADING (no action — self-resolves)
ELSE IF consecutive bad windows >= 3:
    → CONFIRMED (full mitigation)
ELSE:
    → SUSPECTED (rate reduction only)
```

### Adaptive Response

| Status | Actions Taken |
|---|---|
| NONE | Restore rate and power toward defaults |
| FADING | No action |
| SUSPECTED | Reduce packet rate |
| CONFIRMED | Switch channel + reduce rate + increase power |

---

## Project Structure

```
jam-protocol/
├── config.h          # All constants and thresholds (single source of truth)
├── jammer.h/c        # Jammer models
├── monitor.h/c       # Sliding window link quality monitor
├── adaptive.h/c      # Decision engine and parameter controller
├── simulation.h/c    # Simulation engine, CSV output
├── main.c            # Entry point
├── test_jammer.c     # Unit test: jammer models
├── test_monitor.c    # Unit test: classification logic
├── test_adaptive.c   # Unit test: full detect-classify-adapt cycle
└── CMakeLists.txt    # Build configuration
```

---

## Building and Running

### Requirements
- CLion 2025+ (recommended) or any GCC-compatible toolchain
- CMake 3.20+
- GCC with C99 support

### Build in CLion
1. Open the project folder in CLion
2. CLion detects `CMakeLists.txt` automatically
3. Click **Reload CMake** if prompted
4. Select `jam_protocol` from the target dropdown
5. Press `Shift+F10` to build and run

### Build from terminal (Linux/Mac)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./jam_protocol
```

### Build from terminal (Windows)
```
gcc -Wall -std=c99 main.c jammer.c monitor.c adaptive.c simulation.c -o jam_protocol.exe -lm
.\jam_protocol.exe
```

---

## Output

Running the simulation produces **9 CSV files** in the build directory:

| File | Contents |
|---|---|
| `adaptive_None.csv` | Per-window metrics, adaptive node, no jammer |
| `adaptive_Constant.csv` | Per-window metrics, adaptive node, constant jammer |
| `adaptive_Random.csv` | Per-window metrics, adaptive node, random jammer |
| `adaptive_Reactive.csv` | Per-window metrics, adaptive node, reactive jammer |
| `baseline_*.csv` | Same, for non-adaptive baseline node |
| `results_comparison.csv` | Summary table across all 8 scenarios |

### Sample results

| Scenario | Avg PDR | Retransmissions | Recovery Time |
|---|---|---|---|
| Adaptive_None | 0.984 | 16 | — |
| Baseline_None | 0.983 | 17 | — |
| Adaptive_Constant | 0.371 | 629 | — |
| Baseline_Constant | 0.380 | 620 | — |
| Adaptive_Random | 0.694 | 306 | 13.3s |
| Baseline_Random | 0.701 | 299 | 14.7s |
| Adaptive_Reactive | 0.500 | 500 | — |
| Baseline_Reactive | 0.494 | 506 | — |

---

## Running Unit Tests

Each module has an independent test target. In CLion, select the target
from the dropdown and press `Shift+F10`:

| Target | Tests |
|---|---|
| `test_jammer` | PDR/RSS values for all 3 jammer types |
| `test_monitor` | Classification: NONE, FADING, SUSPECTED, CONFIRMED |
| `test_adaptive` | Full detect-classify-adapt cycle, 7 scenarios |

---

## Key Configuration (config.h)

| Constant | Default | Description |
|---|---|---|
| `TOTAL_PACKETS` | 1000 | Packets per simulation run |
| `WINDOW_SIZE` | 20 | Sliding window size |
| `PDR_THRESHOLD` | 0.75 | Below this triggers mitigation |
| `RSS_DROP_TOLERANCE` | 10 dBm | Fading vs jamming discriminator |
| `PERSIST_WINDOWS` | 3 | Windows before jamming confirmed |
| `DEFAULT_TX_POWER` | 20 dBm | Starting transmit power |
| `NUM_CHANNELS` | 12 | Available frequency channels |

---

## References

1. A. Goldsmith, *Wireless Communications*. Cambridge University Press, 2005.
2. T. S. Rappaport, *Wireless Communications: Principles and Practice*, 2nd ed. Prentice Hall, 2002.
3. I. F. Akyildiz et al., "Cognitive radio networks: A survey," *Computer Networks*, vol. 50, no. 13, 2006.
4. M. Strasser et al., "Jamming-resistant communication protocols," *IEEE Transactions on Mobile Computing*, vol. 9, no. 4, 2010.
5. Y. Wu et al., "Anti-jamming techniques in wireless networks," *IEEE Journal on Selected Areas in Communications*, vol. 30, no. 1, 2012.

---

## License

This project was developed as part of the Computer Networks Lab coursework at
Manipal Institute of Technology. For academic use only.