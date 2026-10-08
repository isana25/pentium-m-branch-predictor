# Pentium M Hybrid Branch Predictor (CBP2)

**Course:** COSC6385 - Computer Architecture  
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
| `src/my_predictor.h` | Predictor code: `pm_predictor` (required) and `cpm_predictor` (extra credit) |
| `src/predict.cc` | Simulator driver; set to create `pm_predictor` |
| `report.md` | Report: design, simplifications, results, discussion |
| `results_gshare.txt` | Output of the provided gshare baseline |
| `results_pm.txt` | Output of the Pentium M hybrid predictor (2-way, required) |
| `results_cpm.txt` | Output of the complete Pentium M predictor (extra credit) |
| `src/trace.h` | Changed gzip path to `/usr/bin/gzip` so traces open on macOS |
| `src/predictor.h` | Fixed `target_prediction ()` to return the address instead of a `bool` (needed for extra-credit target results) |

## Results: required predictor (2-way global table)

**Note:** The comparison with gshare is not required by the assignment. I added it myself so the Pentium M hybrid predictor's results have a reference point.

Both predictors do the same job: for every conditional branch in the trace, they guess whether it will be taken or not taken before the real outcome is known. The simulator counts the wrong guesses and reports them as MPKI (mispredictions per 1000 instructions). The gshare predictor provided with CBP2 (32,768 counters, 15-bit history) was used as the baseline, and both were run on the same 20 traces. Average MPKI (lower is better):

These results are for the required predictor (`pm_predictor`), which uses a **2-way** global table.

| Predictor | Storage | Average MPKI |
|---|---|---|
| gshare (provided baseline) | 8 KB | 6.305 |
| Pentium M hybrid, 2-way global (this work) | ≈2.2 KB | 7.259 |

**What the comparison shows:**

- On average, the hybrid predictor makes about 0.95 more mispredictions per 1000 instructions than gshare, while using about one quarter of the storage.
- It is better than gshare on 1 trace (bzip2: 0.076 vs 0.094), and nearly the same on 4 traces (vortex, gzip, jess, parser, each within 0.35 MPKI).
- The largest gaps are on gcc (+4.044), crafty (+2.627), perlbmk (+2.146) and mcf (+1.843). These programs have many different branches. The hybrid's global table has only 1024 entries, so useful history patterns get evicted and the predictor falls back to the simpler bimodal table, while gshare's 32,768 counters can hold far more patterns.
- The gap between the two is small, which suggests the hybrid is working correctly. A broken predictor would show much higher MPKI.

Per-trace results and discussion are in `report.md`.

## Extra credit: complete Pentium M predictor, 4-way (`cpm_predictor`)

**What the extra credit asks for:** the `cpm_predictor` class, described in `my_predictor.h` as the complete Pentium M branch prediction unit, implementing both branch **target** prediction and branch **outcome** prediction. That means adding everything the required version was allowed to skip: the **4-way** global table, the **PIR** path history, the **loop predictor**, and the **BTB / iBTB** for targets.

The `cpm_predictor` class in `src/my_predictor.h` implements all of these. The required `pm_predictor` and its results above are unchanged.

| Part | Required version (`pm_predictor`) | Extra credit (`cpm_predictor`) |
|---|---|---|
| Global table | 512 sets × 2 ways, hashed with a 15-bit outcome history | 512 sets × **4 ways**, hashed with the 15-bit **PIR** (path history: `PIR = (PIR << 2) XOR IP[18:4]` on every taken branch) |
| Loop predictor | — | 64 sets × 2 ways; count, limit and prediction bit per entry, plus a 2-bit confidence counter |
| Bimodal table | 4096 counters | 4096 counters |
| Target prediction | — (always 0) | **BTB** (512 sets × 4 ways, LRU), **iBTB** for indirect branches (256 entries), and a 16-entry return address stack |

Final direction: a confirmed loop prediction first (when the loop predictor and BTB both hit), otherwise the global table on a hit, otherwise the bimodal table. Global entries are allocated only when the loop/bimodal prediction was wrong, as in the real Pentium M.

Some design choices are mine, not from the diagram:
- the loop predictor's 2-bit confidence counter;
- letting a confirmed loop prediction take priority over the global table (path history cannot count more than about 8 loop iterations, so the global table would otherwise hide the loop predictor);
- when a branch is predicted not taken, predicting its fall-through address. Hardware knows this from the instruction length; the trace does not record lengths, so each branch's fall-through address is learned the first time it is not taken.

The return address stack is part of the real Pentium M but is not drawn in the assignment's diagram.

### Extra-credit results (4-way global table)

Run on the same 20 traces (lower is better). The extra-credit program reports two numbers per trace: direction MPKI, and target MPKI (wrong next-address predictions per 1000 instructions, counted on conditional branches).

| Predictor | Average direction MPKI | Average target MPKI |
|---|---|---|
| gshare (provided baseline) | 6.305 | — (not predicted) |
| Pentium M hybrid, 2-way (`pm_predictor`, required) | 7.259 | — (not predicted) |
| Complete Pentium M, 4-way (`cpm_predictor`, extra credit) | 7.424 | 8.179 |

**What these results show:**

- **Direction:** the complete version beats the 2-way version on 13 of 20 traces and beats gshare on 8 (crafty, compress, jess, raytrace, javac, mpegaudio, jack, eon, twolf among them). The 4-way global table keeps more history patterns, which helps most on crafty (−2.522), eon (−1.514) and jack (−1.214). Its average is slightly worse (7.424 vs 7.259) because of three traces: mcf (+8.367), gzip (+2.776) and gap (+2.075). The path history (PIR) only records taken branches, as in the real Pentium M, so it loses the not-taken outcomes these programs' branches depend on.
- **Target:** the average target MPKI (8.179) is only 0.755 above the direction MPKI. Every wrong direction also means a wrong next address, so the extra 0.755 comes from branches whose direction was right but whose target was missing from the BTB. That gap is largest on the Java-like traces with many branches: raytrace (+2.860), javac (+2.299) and mpegaudio (+2.247).

Full per-trace results are in `results_cpm.txt` and in `report.md`.

### How to run the extra credit

```bash
cd src && rm -f predict_extra_credit && make && cd ..
for f in traces/*/*.trace.bz2; do printf "%-45s %s\n" "$f" "$(./src/predict_extra_credit $f)"; done > results_cpm.txt
```

The provided `run_extra` script expects one number per trace, but the extra-credit program prints two (direction and target), so the script cannot average them. The loop above runs the program on each trace instead.

**Fix in `src/predictor.h`:** the course file declared `bool target_prediction ()`, which turns every predicted target address into 0 or 1. It was changed to `unsigned int target_prediction ()`. This does not affect any direction results.

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
