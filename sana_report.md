# Assignment 1: Pentium M Hybrid Branch Predictor

**Name:** Sana Ashfaq   
**Course:** COSC6385 - Computer Architecture  
**Repository:** https://github.com/isana25/pentium-m-branch-predictor

## 1. Goal

The goal of this assignment is to simulate part of the Pentium M dynamic branch predictor inside the CBP2 (Championship Branch Prediction 2) trace-driven infrastructure, and then measure how accurately it predicts branch directions (taken / not taken) on the provided traces. The design follows the Pentium M structure reverse-engineered by Uzelac and Milenković [1]. Only the outcome (direction) predictor is implemented.

## 2. Design

The predictor is implemented as the `pm_predictor` class in `src/my_predictor.h`. It is a hybrid of two components:

### 2.1 Bimodal table
- 4096 two-bit saturating counters, indexed by `IP[11:0]` (the low 12 bits of the branch address).
- Every counter starts at 1 (weakly not taken).
- It is updated on every conditional branch.

### 2.2 Global predictor
- A set-associative table with **512 sets × 2 ways**.
- Each entry stores a valid bit, a 6-bit tag, and a 2-bit saturating counter.
- A 15-bit **Global History Register (GHR)** records the outcomes of the most recent conditional branches.
- Hash: `HASH = IP[14:0] XOR GHR`
  - Set index: `HASH[14:6]` (9 bits → 512 sets)
  - Tag: `HASH[5:0]` (6 bits)
- Replacement: one LRU bit per set. On a miss, the new entry goes into an invalid way if one exists; otherwise it goes into the LRU way.
- Allocation: a new entry is created on every global miss, following the class example in Appendix B. New counters start weakly toward the actual outcome: 2 if the branch was taken, 1 if it was not.

### 2.3 Final prediction
- If the global table has a **tag hit** in either way, the prediction comes from that entry's counter.
- Otherwise the prediction comes from the bimodal counter.
- This matches the Pentium M rule that the global predictor overrides the bimodal predictor whenever it hits.

### 2.4 Update (after the real outcome is known)
1. Train the bimodal counter.
2. On a global hit, train that entry's counter and mark the other way as LRU. On a global miss, allocate an entry as described above.
3. Shift the outcome into the GHR.

Unconditional branches, calls, returns, and indirect branches are not predicted. The predictor returns "taken" for them and does not update any state.

### 2.5 Storage cost

| Structure | Size |
|---|---|
| Bimodal table | 4096 × 2 bits = 8,192 bits |
| Global table | 512 × 2 × (1 + 6 + 2) bits = 9,216 bits |
| LRU bits | 512 bits |
| GHR | 15 bits |
| **Total** | **17,935 bits ≈ 2.2 KB** |

For comparison, the provided gshare baseline uses 32,768 × 2 bits = 65,536 bits (8 KB), which is about 3.7× more storage.

## 3. Simplifications (allowed by the assignment)

- **No loop predictor:** the loop branch predictor buffer (LPB) is not implemented.
- **No target prediction:** the BTB and indirect BTB are not implemented, and the target prediction is always 0.
- **No PIR:** the Path Information Register and its hash are replaced by a plain global history register XORed with the branch address, which is the same idea as gshare.
- **2 ways instead of 4** in the global predictor.

All four of these are implemented in the extra-credit version (Section 7), which uses a 4-way global table.

## 4. How to build and run

```bash
cd cbp2-infrastructure-v2/src
make
cd ..
./run traces > results_pm.txt
```

The gshare comparison (extra, not required) was produced the same way with `gshare_predictor` selected in `predict.cc`, saved to `results_gshare.txt`.

On macOS, one line in `src/trace.h` was changed from `/bin/gzip` to `/usr/bin/gzip` so the traces could be decompressed.

## 5. Results: required predictor (2-way global table)

All results in this section come from the required `pm_predictor`, which uses a **2-way** global table (Section 2).

The metric is MPKI (direction mispredictions per 1000 instructions), as printed by `predict.cc`. Each trace is 100 million instructions. Lower is better.

**Note:** The assignment only requires results for the Pentium M hybrid predictor. The comparison with gshare (the predictor provided with CBP2) is an extra step I added so the results have a reference point. The "Difference" column is hybrid minus gshare; a positive number means the hybrid made more mispredictions.

| Trace | gshare (8 KB) | Pentium M hybrid, 2-way (2.2 KB) | Difference |
|---|---|---|---|
| 164.gzip | 12.473 | 12.617 | +0.144 |
| 175.vpr | 13.415 | 14.337 | +0.922 |
| 176.gcc | 11.254 | 15.298 | +4.044 |
| 181.mcf | 15.837 | 17.680 | +1.843 |
| 186.crafty | 5.837 | 8.464 | +2.627 |
| 197.parser | 10.008 | 10.357 | +0.349 |
| 201.compress | 7.831 | 8.199 | +0.368 |
| 202.jess | 1.562 | 1.750 | +0.188 |
| 205.raytrace | 2.756 | 3.421 | +0.665 |
| 209.db | 3.909 | 4.613 | +0.704 |
| 213.javac | 2.267 | 2.638 | +0.371 |
| 222.mpegaudio | 2.188 | 2.604 | +0.416 |
| 227.mtrt | 2.657 | 3.268 | +0.611 |
| 228.jack | 3.033 | 3.946 | +0.913 |
| 252.eon | 1.807 | 3.139 | +1.332 |
| 253.perlbmk | 2.554 | 4.700 | +2.146 |
| 254.gap | 3.926 | 4.797 | +0.871 |
| 255.vortex | 1.222 | 1.239 | +0.017 |
| 256.bzip2 | 0.094 | 0.076 | −0.018 |
| 300.twolf | 21.489 | 22.046 | +0.557 |
| **Average** | **6.305** | **7.259** | **+0.954** |

## 6. Discussion of the 2-way results

- **Overall:** The hybrid predictor averaged 7.259 MPKI, compared with 6.305 MPKI for gshare: about 0.95 more mispredictions per 1000 instructions, while using about one quarter of the storage (2.2 KB vs 8 KB). The gap is small, which suggests the predictor is working as intended; a broken predictor would show much higher MPKI.
- **Where the hybrid did better or tied:** It beat gshare on 1 trace (bzip2) and was nearly the same on 4 (vortex +0.017, gzip +0.144, jess +0.188, parser +0.349). These programs are dominated by branches that are strongly biased toward one direction, or by a small set of branches. The bimodal table predicts those well, and the small global table is enough for the rest.
- **Where the hybrid did worse:** The largest gaps were on gcc (+4.044), crafty (+2.627), perlbmk (+2.146) and mcf (+1.843). These programs execute many different branches whose outcomes depend on the path taken to reach them. Predicting them well needs many history patterns stored at once. The hybrid's global table holds only 1024 entries (512 sets × 2 ways), so useful entries get evicted and the predictor falls back to the bimodal table, which ignores history. Gshare's 32,768 counters can hold far more patterns.
- **Effect of the simplifications:** Using 2 ways instead of 4 halves the global table, which makes the capacity problem above worse. The full Pentium M also has a loop predictor and allocates global entries only when bimodal mispredicts, which saves room for hard branches. Adding these would likely close part of the gap with gshare.

## 7. Extra credit: complete Pentium M predictor (4-way global table)

The `cpm_predictor` class in `src/my_predictor.h` implements the parts that Section 3 lists as skipped in the required version. The required `pm_predictor` and its results in Section 5 are unchanged.

### 7.1 What the extra credit asks for

The assignment's extra credit is the `cpm_predictor` class, described in `my_predictor.h` as the **complete Pentium M branch prediction unit**, implementing **both branch target prediction and branch outcome prediction**. Compared with the required version, this means adding everything the required version was allowed to skip (Section 3):

- the **4-way** global predictor (instead of 2-way);
- the **Path Information Register (PIR)** and its hash with the branch address;
- the **loop predictor**;
- **branch target prediction**: the BTB and the indirect BTB (iBTB), following Appendix A.

### 7.2 Design

- **Path Information Register (PIR):** 15 bits, updated on every taken branch as `PIR = (PIR << 2) XOR IP[18:4]`. The hash is `HASH = IP[18:4] XOR PIR`. This replaces the outcome history used in the required version.
- **Global predictor:** 512 sets × **4 ways**, set index `HASH[14:6]`, tag `HASH[5:0]`, LRU replacement. A global entry is allocated only when the loop/bimodal prediction was wrong, as in the real Pentium M.
- **Loop predictor:** 64 sets × 2 ways, set index `IP[9:4]`, tag `IP[15:10]`. Each entry holds a 6-bit iteration count, a 6-bit limit (the learned trip count) and a 1-bit prediction (the direction taken while inside the loop). It predicts the loop exit when the count reaches the limit.
- **Bimodal table:** 4096 two-bit counters, as in the required version.
- **Final direction:** a confirmed loop prediction (loop predictor and BTB both hit), otherwise the global table on a hit, otherwise the bimodal table.
- **Target prediction (Appendix A):** a BTB of 512 sets × 4 ways (set index `IP[12:4]`, tag `IP[21:13]`, offset `IP[3:0]`, storing the branch type and target, LRU replacement); an iBTB of 256 entries (index `HASH[13:6]`, 7-bit tag) for indirect branches; and a 16-entry return address stack for returns.

Design choices that are mine rather than from the diagram:

- **Loop confidence:** a 2-bit counter per loop entry; the loop predictor is used only after the same trip count has been seen twice in a row.
- **Loop priority:** a confirmed loop prediction takes priority over the global table. The PIR can only tell apart about 8 taken branches, so it cannot count longer loops, and without this the global table would hide the loop predictor.
- **Fall-through address:** when a branch is predicted not taken, the predicted target is the next instruction. Hardware knows it from the instruction length; the trace does not record lengths, so each branch's fall-through address is learned the first time it is not taken.
- **Return address stack:** part of the real Pentium M, but not drawn in the assignment's diagram.

### 7.3 Changes needed to run it

- `src/predictor.h` declared `bool target_prediction ()`, which turns every predicted target address into 0 or 1. It was changed to `unsigned int target_prediction ()`. This does not affect any direction results.
- The provided `run_extra` script expects one number per trace, but the extra-credit program prints two (direction and target MPKI). The program was run on each trace with a shell loop instead, saving the output to `results_cpm.txt`.

### 7.4 Results: complete predictor (4-way global table)

The last two columns come from the extra-credit `cpm_predictor` with the **4-way** global table. The gshare and 2-way columns are repeated from Section 5 for comparison. Direction and target MPKI (lower is better). Target MPKI counts wrong next-address predictions on conditional branches.

| Trace | gshare | 2-way, required (`pm_predictor`) | 4-way complete: direction | 4-way complete: target |
|---|---|---|---|---|
| 164.gzip | 12.473 | 12.617 | 15.393 | 15.450 |
| 175.vpr | 13.415 | 14.337 | 14.411 | 14.968 |
| 176.gcc | 11.254 | 15.298 | 14.408 | 16.063 |
| 181.mcf | 15.837 | 17.680 | 26.047 | 26.090 |
| 186.crafty | 5.837 | 8.464 | 5.942 | 6.501 |
| 197.parser | 10.008 | 10.357 | 11.204 | 11.294 |
| 201.compress | 7.831 | 8.199 | 7.520 | 7.581 |
| 202.jess | 1.562 | 1.750 | 1.445 | 1.839 |
| 205.raytrace | 2.756 | 3.421 | 2.624 | 5.484 |
| 209.db | 3.909 | 4.613 | 4.147 | 4.974 |
| 213.javac | 2.267 | 2.638 | 2.022 | 4.321 |
| 222.mpegaudio | 2.188 | 2.604 | 1.955 | 4.202 |
| 227.mtrt | 2.657 | 3.268 | 2.808 | 3.895 |
| 228.jack | 3.033 | 3.946 | 2.732 | 3.940 |
| 252.eon | 1.807 | 3.139 | 1.625 | 1.757 |
| 253.perlbmk | 2.554 | 4.700 | 4.395 | 4.703 |
| 254.gap | 3.926 | 4.797 | 6.872 | 6.937 |
| 255.vortex | 1.222 | 1.239 | 1.711 | 2.263 |
| 256.bzip2 | 0.094 | 0.076 | 0.094 | 0.131 |
| 300.twolf | 21.489 | 22.046 | 21.124 | 21.191 |
| **Average** | **6.305** | **7.259** | **7.424** | **8.179** |

### 7.5 Discussion of the 4-way results

- **Direction:** the complete version beats the 2-way version on 13 of 20 traces and beats gshare on 8. The 4-way global table keeps more history patterns, which helps most on crafty (−2.522 vs the 2-way version), eon (−1.514) and jack (−1.214). Its average is slightly worse than the 2-way version (7.424 vs 7.259) because of three traces: mcf (+8.367), gzip (+2.776) and gap (+2.075). The PIR only records taken branches, as in the real Pentium M, so it loses information about not-taken outcomes that the branches in these programs depend on. The outcome history in the required version keeps that information.
- **Target:** the average target MPKI (8.179) is only 0.755 above the direction MPKI (7.424). A wrong direction always means a wrong next address, so the extra 0.755 comes from branches whose direction was right but whose target was missing from the BTB. That gap is largest on raytrace (+2.860), javac (+2.299) and mpegaudio (+2.247), which execute many different branches, so more of them miss in the BTB.

## 8. Code and references

**Code used**

- The simulation infrastructure (`cbp2-infrastructure-v2`) and the `gshare_predictor` baseline were provided by the course [3].
- No external cache code was used; the set-associative tables (global table, loop predictor, BTB) and LRU logic were written directly in `my_predictor.h`.
- The predictor structure follows the Pentium M design reported in [1]; the reverse-engineering method behind it is described in [2].

**References**

1. V. Uzelac and A. Milenković, "Experiment Flows and Microbenchmarks for Reverse Engineering of Branch Predictor Structures," *2009 IEEE International Symposium on Performance Analysis of Systems and Software (ISPASS)*, pp. 207–217, 2009. https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=4919652
2. M. Milenković, A. Milenković and J. Kulick, "Demystifying Intel Branch Predictors," *Workshop on Duplicating, Deconstructing and Debunking (WDDD)*, Anchorage, Alaska, May 2002. https://pharm.ece.wisc.edu/wddd/2002/final/milenkovic.pdf
3. D. A. Jiménez, "Infrastructure for Branch Prediction Competition (CBP2)," 2006. https://people.engr.tamu.edu/djimenez/taco/utsa-www/cs5513/competition/cbp2-infrastructure-v2/doc/index.html
