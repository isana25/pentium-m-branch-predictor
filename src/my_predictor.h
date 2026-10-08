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
class cpm_update : public branch_update {
public:
        unsigned int index;
};

class cpm_predictor : public branch_predictor {
public:
        cpm_update u;

        cpm_predictor (void) {
        }

        branch_update *predict (branch_info & b) {
            u.direction_prediction (true);
            u.target_prediction (0);
            return &u;
        }

        void update (branch_update *u, bool taken, unsigned int target) {
        }

};