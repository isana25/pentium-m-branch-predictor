// my_predictor.h
// This file contains a sample gshare_predictor class.
// It is a simple 32,768-entry gshare with a history length of 15.

class gshare_update : public branch_update {
public:
	unsigned int index;
};

class gshare_predictor : public branch_predictor {
public:
#define HISTORY_LENGTH	15
#define TABLE_BITS	15
	gshare_update u;
	branch_info bi;
	unsigned int history;
	unsigned char tab[1<<TABLE_BITS];

	gshare_predictor (void) : history(0) { 
		memset (tab, 0, sizeof (tab));
	}

	branch_update *predict (branch_info & b) {
		bi = b;
		if (b.br_flags & BR_CONDITIONAL) {
			u.index = 
				  (history << (TABLE_BITS - HISTORY_LENGTH)) 
				^ (b.address & ((1<<TABLE_BITS)-1));
			u.direction_prediction (tab[u.index] >> 1);
		} else {
			u.direction_prediction (true);
		}
		u.target_prediction (0);
		return &u;
	}

	void update (branch_update *u, bool taken, unsigned int target) {
		if (bi.br_flags & BR_CONDITIONAL) {
			unsigned char *c = &tab[((gshare_update*)u)->index];
			if (taken) {
				if (*c < 3) (*c)++;
			} else {
				if (*c > 0) (*c)--;
			}
			history <<= 1;
			history |= taken;
			history &= (1<<HISTORY_LENGTH)-1;
		}
	}
};

//
// Pentium M hybrid branch predictors
// This class implements a simple hybrid branch predictor based on the Pentium M branch outcome prediction units. 
// Instead of implementing the complete Pentium M branch outcome predictors, the class below implements a hybrid 
// predictor that combines a bimodal predictor and a global predictor. 
//
// Structure (follows the Pentium M diagram in the assignment):
//   Bimodal table : 4096 2-bit counters, index = IP[11:0]
//   Global table  : 512 sets x 2 ways (assignment allows 2 ways instead of 4)
//                   each entry = valid bit, 6-bit tag, 2-bit counter
//                   HASH  = IP[14:0] XOR GHR (15-bit global history register;
//                           stands in for the PIR, which is not required)
//                   index = HASH[14:6], tag = HASH[5:0]
//   Final prediction: global counter if tag hit, otherwise bimodal counter.
//
class pm_update : public branch_update {
public:
        unsigned int index;     // bimodal table index
        unsigned int g_set;     // global table set index
        unsigned int g_tag;     // global table tag
        int g_way;              // way that hit, or -1 on global miss
        bool bimodal_pred;      // what the bimodal table predicted
};

class pm_predictor : public branch_predictor {
public:
#define PM_BIM_BITS     12      // 4096-entry bimodal table
#define PM_HIST_BITS    15      // GHR length = hash width
#define PM_SET_BITS     9       // 512 sets
#define PM_TAG_BITS     6       // 6-bit tag
#define PM_WAYS         2       // 2-way (the LRU logic below assumes 2)

// Allocation policy on a global miss:
//   1 = allocate a global entry on every miss (matches the class example in Appendix B)
//   0 = allocate only when the bimodal table mispredicted (closer to real Pentium M)
#define PM_ALLOC_ON_EVERY_MISS 1

        struct g_entry {
                bool valid;
                unsigned char tag;
                unsigned char ctr;
        };

        pm_update u;
        branch_info bi;
        unsigned int ghr;
        unsigned char bim[1<<PM_BIM_BITS];
        g_entry gtab[1<<PM_SET_BITS][PM_WAYS];
        unsigned char lru[1<<PM_SET_BITS];     // which way is least recently used in each set

        pm_predictor (void) : ghr(0) {
                for (int i = 0; i < (1<<PM_BIM_BITS); i++) bim[i] = 1;  // weakly not taken
                memset (gtab, 0, sizeof (gtab));
                memset (lru, 0, sizeof (lru));
        }

        // saturating 2-bit counter update
        static void sat (unsigned char &c, bool taken) {
                if (taken) { if (c < 3) c++; }
                else       { if (c > 0) c--; }
        }

        branch_update *predict (branch_info & b) {
                bi = b;
                if (b.br_flags & BR_CONDITIONAL) {
                        // bimodal lookup
                        u.index = b.address & ((1<<PM_BIM_BITS)-1);
                        u.bimodal_pred = bim[u.index] >> 1;

                        // global lookup
                        unsigned int h = (b.address ^ ghr) & ((1<<PM_HIST_BITS)-1);
                        u.g_set = (h >> PM_TAG_BITS) & ((1<<PM_SET_BITS)-1);
                        u.g_tag = h & ((1<<PM_TAG_BITS)-1);
                        u.g_way = -1;
                        for (int w = 0; w < PM_WAYS; w++) {
                                g_entry &e = gtab[u.g_set][w];
                                if (e.valid && e.tag == u.g_tag) { u.g_way = w; break; }
                        }

                        // final prediction: global on hit, bimodal otherwise
                        if (u.g_way >= 0)
                                u.direction_prediction (gtab[u.g_set][u.g_way].ctr >> 1);
                        else
                                u.direction_prediction (u.bimodal_pred);
                } else {
                        u.direction_prediction (true);
                }

                // branch target prediction is skipped
                u.target_prediction (0);
                return &u;
        }

        void update (branch_update *bu, bool taken, unsigned int target) {
                if (!(bi.br_flags & BR_CONDITIONAL)) return;
                pm_update *p = (pm_update*) bu;

                // bimodal table is always trained
                sat (bim[p->index], taken);

                if (p->g_way >= 0) {
                        // global hit: train that counter, mark the other way as LRU
                        sat (gtab[p->g_set][p->g_way].ctr, taken);
                        lru[p->g_set] = 1 - p->g_way;
                } else if (PM_ALLOC_ON_EVERY_MISS || p->bimodal_pred != taken) {
                        // global miss: allocate into an invalid way if any, else the LRU way
                        int w = lru[p->g_set];
                        for (int i = 0; i < PM_WAYS; i++)
                                if (!gtab[p->g_set][i].valid) { w = i; break; }
                        g_entry &e = gtab[p->g_set][w];
                        e.valid = true;
                        e.tag = p->g_tag;
                        e.ctr = taken ? 2 : 1;          // weakly toward the actual outcome
                        lru[p->g_set] = 1 - w;
                }

                // shift the outcome into the global history register
                ghr = ((ghr << 1) | (taken ? 1 : 0)) & ((1<<PM_HIST_BITS)-1);
        }

};

//
// Complete Pentium M branch predictors for extra credit
// This class implements the complete Pentium M branch prediction units. 
// It implements both branch target prediction and branch outcome predicton. 
//
// Structure (follows the Pentium M diagrams in the assignment, pages 2 and 4):
//   PIR    : 15-bit Path Information Register, updated on every taken branch:
//            PIR = (PIR << 2) XOR IP[18:4]
//   HASH   : IP[18:4] XOR PIR (15 bits)
//   Outcome (direction) prediction:
//     Bimodal table : 4096 2-bit counters, index = IP[11:0]
//     Loop predictor: 64 sets x 2 ways, index = IP[9:4], tag = IP[15:10],
//                     each entry = count (6b), limit (6b), prediction (1b)
//                     (+ a 2-bit confidence counter, see below)
//     Global table  : 512 sets x 4 ways, index = HASH[14:6], tag = HASH[5:0]
//     Final outcome : loop if (loop hit AND BTB hit AND the loop's trip count is confirmed);
//                     else global if global hit;
//                     else bimodal
//                     (Design choice: a confirmed loop prediction takes priority over the
//                      global table, because the path history cannot count loop iterations
//                      beyond ~8 taken branches and would otherwise hide the loop predictor.)
//   Target prediction (Appendix A):
//     BTB  : 512 sets x 4 ways, index = IP[12:4], tag = IP[21:13], offset = IP[3:0],
//            stores branch type and target; LRU replacement
//     iBTB : 256 entries, index = HASH[13:6], tag = {HASH[14], HASH[5:0]} (7 bits),
//            used for indirect branches when the BTB hits
//     RAS  : 16-entry return address stack for returns (the real Pentium M
//            has one; it is not drawn in the assignment's diagram)
//     Not-taken prediction: the fall-through address (learned per branch, see ft_addr)
//
class cpm_update : public branch_update {
public:
        unsigned int index;             // bimodal index
        unsigned int hash;              // IP[18:4] XOR PIR at prediction time
        unsigned int g_set, g_tag;      // global table set / tag
        int g_way;                      // global way that hit, -1 = miss
        unsigned int l_set, l_tag;      // loop predictor set / tag
        int l_way;                      // loop way that hit, -1 = miss
        unsigned int b_set, b_tag, b_off; // BTB set / tag / offset
        int b_way;                      // BTB way that hit, -1 = miss
        unsigned int i_set, i_tag;      // iBTB index / tag
        bool bimodal_pred;              // bimodal prediction
        bool local_pred;                // loop-or-bimodal prediction (before the global override)
};

class cpm_predictor : public branch_predictor {
public:
#define CPM_BIM_BITS    12      // 4096-entry bimodal table
#define CPM_HASH_BITS   15      // PIR / HASH width
#define CPM_G_SETS      512     // global: 512 sets
#define CPM_G_WAYS      4       // global: 4 ways (full Pentium M)
#define CPM_L_SETS      64      // loop predictor: 64 sets
#define CPM_L_WAYS      2       // loop predictor: 2 ways
#define CPM_L_MAX       63      // 6-bit count / limit
#define CPM_L_CONF      2       // trip count must repeat this many times before the loop predictor is trusted
#define CPM_B_SETS      512     // BTB: 512 sets
#define CPM_B_WAYS      4       // BTB: 4 ways
#define CPM_I_SETS      256     // iBTB: 256 entries
#define CPM_RAS_SIZE    16      // return address stack
#define CPM_FT_BITS     12      // fall-through address table (see below)

// Global table allocation on a global miss:
//   0 = allocate only when the loop/bimodal prediction was wrong (default; as in the real
//       Pentium M, so the global table is kept for branches the simpler predictors miss)
//   1 = allocate on every miss (same as pm_predictor)
#define CPM_ALLOC_ON_EVERY_MISS 0

        struct g_entry { bool valid; unsigned char tag, ctr; unsigned int stamp; };
        struct l_entry { bool valid, pred; unsigned char tag, count, limit, conf; unsigned int stamp; };
        struct b_entry { bool valid; unsigned short tag; unsigned char off, type; unsigned int target, stamp; };
        struct i_entry { bool valid; unsigned char tag; unsigned int target; };

        cpm_update u;
        branch_info bi;
        unsigned int pir;
        unsigned int clock;             // time stamp for LRU
        unsigned char bim[1<<CPM_BIM_BITS];
        g_entry gtab[CPM_G_SETS][CPM_G_WAYS];
        l_entry ltab[CPM_L_SETS][CPM_L_WAYS];
        b_entry btb[CPM_B_SETS][CPM_B_WAYS];
        i_entry ibtb[CPM_I_SETS];
        unsigned int ras[CPM_RAS_SIZE];
        int ras_top, ras_count;
        // Fall-through addresses. When a conditional branch is predicted not taken, the next
        // fetch address is the instruction after the branch (branch address + instruction
        // length). Real hardware knows the length from the decoder; the trace does not record
        // it, so each branch's fall-through address is learned the first time it is not taken.
        unsigned int ft_addr[1<<CPM_FT_BITS], ft_tgt[1<<CPM_FT_BITS];

        cpm_predictor (void) : pir(0), clock(0), ras_top(0), ras_count(0) {
                for (int i = 0; i < (1<<CPM_BIM_BITS); i++) bim[i] = 1;   // weakly not taken
                memset (gtab, 0, sizeof (gtab));
                memset (ltab, 0, sizeof (ltab));
                memset (btb, 0, sizeof (btb));
                memset (ibtb, 0, sizeof (ibtb));
                memset (ras, 0, sizeof (ras));
                memset (ft_addr, 0, sizeof (ft_addr));
                memset (ft_tgt, 0, sizeof (ft_tgt));
        }

        static void sat (unsigned char &c, bool taken) {
                if (taken) { if (c < 3) c++; }
                else       { if (c > 0) c--; }
        }

        // pick a way to replace: an invalid way if any, else the least recently used
        template <class E> static int victim (E *set, int ways) {
                int v = 0;
                for (int i = 0; i < ways; i++) {
                        if (!set[i].valid) return i;
                        if (set[i].stamp < set[v].stamp) v = i;
                }
                return v;
        }

        branch_update *predict (branch_info & b) {
                bi = b;
                unsigned int ip = b.address;
                u.hash = ((ip >> 4) ^ pir) & ((1<<CPM_HASH_BITS)-1);

                // ---- BTB lookup (all branch types) ----
                u.b_set = (ip >> 4) & (CPM_B_SETS-1);
                u.b_tag = (ip >> 13) & 0x1FF;
                u.b_off = ip & 0xF;
                u.b_way = -1;
                for (int w = 0; w < CPM_B_WAYS; w++) {
                        b_entry &e = btb[u.b_set][w];
                        if (e.valid && e.tag == u.b_tag && e.off == u.b_off) { u.b_way = w; break; }
                }

                // ---- iBTB index / tag ----
                u.i_set = (u.hash >> 6) & (CPM_I_SETS-1);
                u.i_tag = (((u.hash >> 14) & 1) << 6) | (u.hash & 0x3F);

                // ---- outcome prediction ----
                u.g_way = -1;
                u.l_way = -1;
                if (b.br_flags & BR_CONDITIONAL) {
                        // bimodal
                        u.index = ip & ((1<<CPM_BIM_BITS)-1);
                        u.bimodal_pred = bim[u.index] >> 1;
                        bool pred = u.bimodal_pred;

                        // loop predictor (used only when the BTB also hits)
                        u.l_set = (ip >> 4) & (CPM_L_SETS-1);
                        u.l_tag = (ip >> 10) & 0x3F;
                        for (int w = 0; w < CPM_L_WAYS; w++) {
                                l_entry &e = ltab[u.l_set][w];
                                if (e.valid && e.tag == u.l_tag) { u.l_way = w; break; }
                        }
                        bool loop_used = false;
                        if (u.l_way >= 0 && u.b_way >= 0) {
                                l_entry &e = ltab[u.l_set][u.l_way];
                                if (e.conf >= CPM_L_CONF) {
                                        pred = (e.count == e.limit) ? !e.pred : e.pred;
                                        loop_used = true;
                                }
                        }
                        u.local_pred = pred;

                        // global predictor (overrides on a hit)
                        u.g_set = (u.hash >> 6) & (CPM_G_SETS-1);
                        u.g_tag = u.hash & 0x3F;
                        for (int w = 0; w < CPM_G_WAYS; w++) {
                                g_entry &e = gtab[u.g_set][w];
                                if (e.valid && e.tag == u.g_tag) { u.g_way = w; break; }
                        }
                        // a confident loop prediction is kept; otherwise a global hit overrides
                        if (u.g_way >= 0 && !loop_used)
                                pred = gtab[u.g_set][u.g_way].ctr >> 1;

                        u.direction_prediction (pred);
                } else {
                        u.direction_prediction (true);
                }

                // ---- target prediction ----
                unsigned int tgt = 0;
                if ((b.br_flags & BR_CONDITIONAL) && !u.direction_prediction ()) {
                        // predicted not taken: next address is the fall-through
                        unsigned int f = ip & ((1<<CPM_FT_BITS)-1);
                        tgt = (ft_addr[f] == ip) ? ft_tgt[f] : ip + 2;   // 2 = short x86 Jcc
                } else if ((b.br_flags & BR_RETURN) && ras_count > 0) {
                        tgt = ras[(ras_top + CPM_RAS_SIZE - 1) % CPM_RAS_SIZE];
                } else if (u.b_way >= 0) {
                        tgt = btb[u.b_set][u.b_way].target;
                        if (b.br_flags & BR_INDIRECT) {
                                i_entry &ie = ibtb[u.i_set];
                                if (ie.valid && ie.tag == u.i_tag) tgt = ie.target;
                        }
                }
                u.target_prediction (tgt);
                return &u;
        }

        void update (branch_update *bu, bool taken, unsigned int target) {
                cpm_update *p = (cpm_update*) bu;
                clock++;

                if (bi.br_flags & BR_CONDITIONAL) {
                        // bimodal
                        sat (bim[p->index], taken);

                        // global
                        if (p->g_way >= 0) {
                                g_entry &e = gtab[p->g_set][p->g_way];
                                sat (e.ctr, taken);
                                e.stamp = clock;
                        } else if (CPM_ALLOC_ON_EVERY_MISS || p->local_pred != taken) {
                                int w = victim (gtab[p->g_set], CPM_G_WAYS);
                                g_entry &e = gtab[p->g_set][w];
                                e.valid = true; e.tag = p->g_tag; e.ctr = taken ? 2 : 1; e.stamp = clock;
                        }

                        // loop predictor
                        if (p->l_way >= 0) {
                                l_entry &e = ltab[p->l_set][p->l_way];
                                e.stamp = clock;
                                if (taken == e.pred) {
                                        // still inside the loop
                                        if (e.count < CPM_L_MAX) e.count++;
                                        else e.valid = false;   // trip count too long for 6 bits
                                } else if (e.count == 0) {
                                        // the "dominant" direction was guessed wrong: flip it
                                        e.pred = taken; e.limit = 0; e.conf = 0;
                                } else {
                                        // loop exit: check the trip count against the stored limit
                                        if (e.count == e.limit) { if (e.conf < 3) e.conf++; }
                                        else { e.limit = e.count; e.conf = 0; }
                                        e.count = 0;
                                }
                        } else if (p->direction_prediction () != taken) {
                                // allocate on a misprediction: assume this outcome is a loop exit
                                int w = victim (ltab[p->l_set], CPM_L_WAYS);
                                l_entry &e = ltab[p->l_set][w];
                                e.valid = true; e.tag = p->l_tag; e.pred = !taken;
                                e.count = 0; e.limit = 0; e.conf = 0; e.stamp = clock;
                        }
                }

                // learn the fall-through address of not-taken conditional branches
                if ((bi.br_flags & BR_CONDITIONAL) && !taken) {
                        unsigned int f = bi.address & ((1<<CPM_FT_BITS)-1);
                        ft_addr[f] = bi.address; ft_tgt[f] = target;
                }

                // BTB: allocate / update on taken branches
                if (taken) {
                        int w = p->b_way;
                        if (w < 0) w = victim (btb[p->b_set], CPM_B_WAYS);
                        b_entry &e = btb[p->b_set][w];
                        e.valid = true; e.tag = p->b_tag; e.off = p->b_off;
                        e.type = bi.br_flags; e.target = target; e.stamp = clock;
                } else if (p->b_way >= 0) {
                        btb[p->b_set][p->b_way].stamp = clock;
                }

                // iBTB: indirect jumps and indirect calls (not returns)
                if ((bi.br_flags & BR_INDIRECT) && !(bi.br_flags & BR_RETURN)) {
                        i_entry &ie = ibtb[p->i_set];
                        ie.valid = true; ie.tag = p->i_tag; ie.target = target;
                }

                // return address stack
                if (bi.br_flags & BR_CALL) {
                        ras[ras_top] = bi.address + ((bi.br_flags & BR_INDIRECT) ? 2 : 5);
                        ras_top = (ras_top + 1) % CPM_RAS_SIZE;
                        if (ras_count < CPM_RAS_SIZE) ras_count++;
                } else if ((bi.br_flags & BR_RETURN) && ras_count > 0) {
                        ras_top = (ras_top + CPM_RAS_SIZE - 1) % CPM_RAS_SIZE;
                        ras_count--;
                }

                // path information register: updated on every taken branch
                if (taken)
                        pir = ((pir << 2) ^ ((bi.address >> 4) & ((1<<CPM_HASH_BITS)-1))) & ((1<<CPM_HASH_BITS)-1);
        }

};