# Pentium M Hybrid Branch Predictor (CBP2)

**Course:** COSC6385 26227 - Computer Architecture  
**Author:** Sana Ashfaq

This repository contains a simplified Pentium M branch outcome predictor implemented in the CBP2 (Championship Branch Prediction 2) simulation infrastructure, for Assignment 1: Branch Predictor Implementation.

## What is implemented

The predictor is the `pm_predictor` class in `src/my_predictor.h`. It combines two components:

- **Bimodal table:** 4096 two-bit counters, indexed by the low 12 bits of the branch address.
- **Global predictor:** 512 sets × 2 ways. Each entry has a valid bit, a 6-bit tag and a 2-bit counter. It is indexed by `(branch address XOR 15-bit global history)`, using bits 14–6 as the set index and bits 5–0 as the tag. Replacement uses LRU, with one bit per set: on a miss, an empty way is filled first, otherwise the least recently used way is replaced.

If the global predictor has a tag hit, its prediction is used. Otherwise the bimodal prediction is used.

The loop predictor, branch target prediction (BTB / iBTB) and the PIR hash are not implemented, as allowed by the assignment. See `report.md` for the full design and results.

## Files

| File | Description |
|---|---|
| `src/my_predictor.h` | Predictor code (the `pm_predictor` class is the assignment's code) |
| `src/predict.cc` | Simulator driver; set to create `pm_predictor` |
| `report.md` | Report: design, simplifications, results, discussion |
| `results_gshare.txt` | Output of the provided gshare baseline |
| `results_pm.txt` | Output of the Pentium M hybrid predictor |
| `src/trace.h` | Changed gzip path to `/usr/bin/gzip` so traces open on macOS |

## Results summary

**Note:** The comparison with gshare is not required by the assignment. I added it myself so the Pentium M hybrid predictor's results have a reference point.

Both predictors do the same job: for every conditional branch in the trace, they guess whether it will be taken or not taken before the real outcome is known. The simulator counts the wrong guesses and reports them as MPKI (mispredictions per 1000 instructions). The gshare predictor provided with CBP2 (32,768 counters, 15-bit history) was used as the baseline, and both were run on the same 20 traces. Average MPKI (lower is better):

| Predictor | Storage | Average MPKI |
|---|---|---|
| gshare (provided baseline) | 8 KB | 6.305 |
| Pentium M hybrid (this work) | ≈2.2 KB | 7.259 |

**What the comparison shows:**

- On average, the hybrid predictor makes about 0.95 more mispredictions per 1000 instructions than gshare, while using about one quarter of the storage.
- It is better than gshare on 1 trace (bzip2: 0.076 vs 0.094), and nearly the same on 4 traces (vortex, gzip, jess, parser, each within 0.35 MPKI).
- The largest gaps are on gcc (+4.044), crafty (+2.627), perlbmk (+2.146) and mcf (+1.843). These programs have many different branches. The hybrid's global table has only 1024 entries, so useful history patterns get evicted and the predictor falls back to the simpler bimodal table, while gshare's 32,768 counters can hold far more patterns.
- The gap between the two is small, which suggests the hybrid is working correctly. A broken predictor would show much higher MPKI.

Per-trace results and discussion are in `report.md`.

## How to build

```bash
cd cbp2-infrastructure-v2/src
make
```

This produces the `predict` binary. If `make` says "Nothing to be done" after editing `my_predictor.h`, force a rebuild with `rm -f predict && make`.

**macOS note:** `src/trace.h` originally used `/bin/gzip`, which does not exist on macOS. It was changed to `/usr/bin/gzip`. Check your paths with `which gzip bzip2` on other systems.

## How to run

From the top-level `cbp2-infrastructure-v2` folder:

```bash
./run traces > results_pm.txt
```

To run the gshare baseline instead, change `pm_predictor` back to `gshare_predictor` in `src/predict.cc`, rebuild with `make`, and run:

```bash
./run traces > results_gshare.txt
```

## Configuration

In `src/my_predictor.h`, `PM_ALLOC_ON_EVERY_MISS` controls when global entries are created:

- `1` (default): allocate on every global miss, as in the class example.
- `0`: allocate only when the bimodal table mispredicts, which is closer to the real Pentium M.

## References

- CBP2 infrastructure, provided by the course (includes the `gshare_predictor` baseline).
- V. Uzelac and A. Milenković, "Experiment Flows and Microbenchmarks for Reverse Engineering of Branch Predictor Structures," ISPASS 2009.
