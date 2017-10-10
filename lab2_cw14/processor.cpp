#include "simulator.hpp"

//row Btb[SETS / WAYS][WAYS];
row *Btb;
long ARF[ARF_ROWS]; // Afector Register File
long ABB; // Affector Branch Bitmap
long GHR; // Global History Register
// int PHT_2bit[PHT_SIZE];
int PHT_abb[PHT_SIZE];
int PHT_abb_nf[PHT_SIZE];
int PHT_abb_64[PHT_SIZE_64];
int PHT_abb_64_nf[PHT_SIZE_64];

int bit2_counter;

/*
Done:
- GHR;
- Function to index PHT from GHR;
- Get values in ARF;
- Actually set ABB;
- Update PHT;
- Create predictions with PHT;
TODO: Known bug: If no ABB try to predict we will not count penalties!
*/

unsigned int Hit, Miss, BtbHit, BtbMiss, TotalPenalty;
unsigned int TotalPenaltyNf, TotalPenalty64, TotalPenalty64Nf;
unsigned int RightCorrection, WrongCorrection;
unsigned int RightCorrectionNf, WrongCorrectionNf;
unsigned int RightCorrection64, WrongCorrection64;
unsigned int RightCorrection64Nf, WrongCorrection64Nf;
unsigned int AbbRight, AbbWrong;
unsigned int AbbNfRight, AbbNfWrong;
unsigned int Abb64Right, Abb64Wrong;
unsigned int Abb64NfRight, Abb64NfWrong;

inline int idx(int base, int deslocamento) {
    return base * WAYS + deslocamento;
}

inline int getBase(uint64_t pc) {
    return (pc >> 2) & TAG_BITS;
}

row* inBtb(uint64_t pc) {
// If we find a row in Btb with this pc and valid == true, return it.
// Otherwise return NULL.
    int base = getBase(pc);
    int i, index = -1;
    for (i=0; i<WAYS; ++i) {
        if (Btb[idx(base,i)].valid && Btb[idx(base,i)].address == pc) {
            index = i;
        }
    }
    if (index > 1) {
        return &(Btb[idx(base, index)]);
    }
    return NULL;
}

row createRow(uint64_t address, int opcode_size) {
    row newRow;
    newRow.address = address;
    newRow.tag = getBase(address);
    newRow.idx = -1;
    newRow.lru = 0;
    newRow.valid = true;
    newRow.bht = 0;
    newRow.target_address = 0;
    newRow.opcode_size = opcode_size;
    return newRow;
}

// BTB
void copy_row(row *dst, row src) {
    dst->tag = src.tag;
    dst->idx = src.idx;
    dst->lru = src.lru;
    dst->address = src.address;
    dst->valid = src.valid;
    dst->bht = src.bht;
    dst->target_address = src.target_address;
    dst->opcode_size = src.opcode_size;
}

void update_phts(int idx_abb, int idx_abb_nf, int idx_abb_64, int idx_abb_64_nf, bool taken) {
    if (taken) {
        PHT_abb[idx_abb] = (PHT_abb[idx_abb] < PHT_ABB_MAX) ? PHT_abb[idx_abb] + 1 : PHT_ABB_MAX;
        PHT_abb_nf[idx_abb_nf] = (PHT_abb_nf[idx_abb_nf] < PHT_ABB_MAX) ? PHT_abb_nf[idx_abb_nf] + 1 : PHT_ABB_MAX;
        PHT_abb_64[idx_abb_64] = (PHT_abb_64[idx_abb_64] < PHT_ABB_MAX) ? PHT_abb_64[idx_abb_64] + 1 : PHT_ABB_MAX;
        PHT_abb_64_nf[idx_abb_64_nf] = (PHT_abb_64_nf[idx_abb_64_nf] < PHT_ABB_MAX) ? PHT_abb_64_nf[idx_abb_64_nf] + 1 : PHT_ABB_MAX;
    } else {
        PHT_abb[idx_abb] = (PHT_abb[idx_abb] == 0) ? 0 : PHT_abb[idx_abb] - 1;
        PHT_abb_nf[idx_abb_nf] = (PHT_abb_nf[idx_abb_nf] == 0) ? 0 : PHT_abb_nf[idx_abb_nf] - 1;
        PHT_abb_64[idx_abb_64] = (PHT_abb_64[idx_abb_64] == 0) ? 0 : PHT_abb_64[idx_abb_64] - 1;
        PHT_abb_64_nf[idx_abb_64_nf] = (PHT_abb_64_nf[idx_abb_64_nf] == 0) ? 0 : PHT_abb_64_nf[idx_abb_64_nf] - 1;
    }
}

void insert_row(row newRow) {
    int base = newRow.tag;

    int i, invalid = -1, menor_lru = WAYS+1, index = -1;
    // Precisamos decidir quem é o cara que eu vou chutar fora.
    // Se achar alguem inválido, é ele. Se não, pega o cara com
    // menor LRU.
    for (i=0; i<WAYS; ++i) {
        if (!(Btb[idx(base, i)]).valid) {
            invalid = i;
            break;
        }
        if (Btb[idx(base,i)].lru < menor_lru) {
            menor_lru = Btb[idx(base,i)].lru;
            index = i;
        }
    }
    if (invalid != -1) {
        index = invalid;
    }
    newRow.valid = true;
    newRow.lru = WAYS-1;
    // Nesse momento eu tenho certeza que idx contém o index certo.
    copy_row(&(Btb[idx(base, index)]), newRow);

    // Atualiza LRUs
    for (i=0; i<WAYS; ++i) {
        --Btb[idx(base,i)].lru;
    }
}
/*
int prediction1(row *branchRow, uint64_t pc) {
    // 1 bit history
    if (Predictor_type == ONE_BIT) {
        if (branchRow->bht == 0) {
            return NOT_TAKEN;
        } else if (branchRow->bht == 1) {
            return TAKEN;
        }
        return -1;
    }

    if (Predictor_type == GSHARE) {
        // GShare faz xor do GHR com PC pra determinar entrada da PHT.
        //return predicao_pht(PHT[])
    }

    return -1;
}
*/

int predictionAbb(int data) {
    // Recebe como parametro uma linha de pht, retorna TAKEN ou NAO_TAKEN.
    if (data >= PHT_ABB_MAX / 2) {
        return TAKEN;
    } else {
        return NOT_TAKEN;
    }
}

int prediction_2bit(int data) {
    if (data >= 2) {
        return TAKEN;
    } else {
        return NOT_TAKEN;
    }
}

int getPhtIdx(long GHR, long ABB) {
    // Long has 64 bits. If it is bigger than PHT_IDX_SIZE, its not going to work.
    // We have to take only a few bits from GHR and ABB. The way sugested in the
    // article is to do a Fold-XOR hash.
    int masked, shifted;
    masked = GHR & ABB; // 1. Mask GHR with ABB.
    masked = masked & 0x7FF; // 2. Get 11 LSB on masked

    shifted = ((GHR & ABB) >> 11) & 0x7FF; // 3. Get bits 11-21.
    masked = masked | shifted; // 4. XOR them.

    shifted = ((GHR & ABB) >> 22) & 0x7FF; // 5. Get next 11 bits
    masked = masked | shifted; // 6. XOR them.

    shifted = ((GHR & ABB) >> 33) & 0x7FF; // 5. Get next 11 bits
    masked = masked | shifted; // 8. XOR them.

    shifted = ((GHR & ABB) >> 44) & 0x7FF; // 5. Get next 11 bits
    masked = masked | shifted; // 8. XOR them.

    shifted = ((GHR & ABB) >> 55) & 0x7FF; // 5. Get next 11 bits
    masked = masked | shifted; // 8. XOR them.

    return masked;
/* Execution Example (with 8 bits, folding 4 times 2 bits):
   GHR =  01111001
   ABB =  11101001 &
          --------
1. mask = 01101001
2. mask = 00000001
3. shifted = 10 (xxxx10xx)
4. mask = 00000011
5. shifted = 10 (xx10xxxx)
6. mask = 00000001
7. shifted = 01 (01xxxxxx)
8. mask = 00000000
*/
}

int getPhtIdxNf(long GHR, long ABB) {
    // Get PhtIdx without Fold-XOR
    int masked;
    masked = GHR & ABB; // 1. Mask GHR with ABB.
    masked = masked & 0x7FF; // 2. Get 11 LSB on masked
    return masked;
}

int getPhtIdx64(long GHR, long ABB) {
    int masked, shifted;
    masked = GHR & ABB; // 1. Mask GHR with ABB.
    masked = masked & 0xFFFF; // 2. Get 11 LSB on masked

    shifted = ((GHR & ABB) >> 16) & 0xFFFF; // 3. Get bits 11-21.
    masked = masked | shifted; // 4. XOR them.

    shifted = ((GHR & ABB) >> 32) & 0xFFFF; // 5. Get next 11 bits
    masked = masked | shifted; // 6. XOR them.

    shifted = ((GHR & ABB) >> 48) & 0xFFFF; // 5. Get next 11 bits
    masked = masked | shifted; // 8. XOR them.

    return masked;
}

int getPhtIdx64Nf(long GHR, long ABB) {
    int masked;
    masked = GHR & ABB; // 1. Mask GHR with ABB.
    masked = masked & 0xFFFF; // 2. Get 11 LSB on masked
    return masked;
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
    Btb = (row*) malloc(sizeof(struct row) * N_ROWS * WAYS);
    int i;
    for (i=0; i<N_ROWS*WAYS; ++i) {
        Btb[i].address = 0;
        Btb[i].valid = false;
        Btb[i].bht = 0;
        Btb[i].lru = 0;
    }
    GHR = 0;
    Hit = 0;
    Miss = 0;
    BtbHit = 0;
    BtbMiss = 0;

    AbbRight = 0;
    AbbWrong = 0;
    AbbNfRight = 0;
    AbbNfWrong = 0;
    Abb64Right = 0;
    Abb64Wrong = 0;
    Abb64NfRight = 0;
    Abb64NfWrong = 0;

    TotalPenalty = 0;
    TotalPenaltyNf = 0;
    TotalPenalty64 = 0;
    TotalPenalty64Nf = 0;

    RightCorrection = 0;
    WrongCorrection = 0;
    RightCorrectionNf = 0;
    WrongCorrectionNf = 0;
    RightCorrection64 = 0;
    WrongCorrection64 = 0;
    RightCorrection64Nf = 0;
    WrongCorrection64Nf = 0;
    for (i=0; i<ARF_ROWS; ++i) {
        ARF[i] = 0;
    }
    for (i=0; i<PHT_SIZE; ++i) {
        PHT_abb[i] = 0;
    }
    for (i=0; i<PHT_SIZE; ++i) {
        PHT_abb_nf[i] = 0;
    }
    for (i=0; i<PHT_SIZE_64; ++i) {
        PHT_abb_64[i] = 0;
    }
    for (i=0; i<PHT_SIZE_64; ++i) {
        PHT_abb_64_nf[i] = 0;
    }
    bit2_counter = 0;
};

// =====================================================================
void processor_t::clock() {
    int i, phtIndex;
    static bool wasBranch = false, wasTaken = false, predicted_abb = false, predicted_2bit = false; // BTB
    static bool predicted_abb_nf = false, predicted_abb_64 = false, predicted_abb_64_nf = false;
    static uint64_t previous_pc = 0; // BTB
    static int previous_instr_size;
    static uint64_t predicted_pc_abb = 0, predicted_pc_abb_nf = 0, predicted_pc_2bit = 0;
    static uint64_t predicted_pc_abb_64 = 0, predicted_pc_abb_64_nf = 0;
    static int abb_prediction = -1, abb_nf_prediction = -1, bit2_prediction = -1;
    static int abb_64_prediction = -1, abb_64_nf_prediction = -1;

    /// Get the next instruction from the trace
    opcode_package_t new_instruction;
    if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
        /// If EOF
        orcs_engine.simulator_alive = false;
        return;
    }

    // TODO: Remover isso...
    if (predicted_pc_abb && predicted_pc_abb_nf && predicted_pc_2bit && predicted_pc_abb_64 && predicted_pc_abb_64_nf) {
        predicted_pc_2bit = predicted_pc_2bit;
    }

    /* BEGIN ARF */
    /* Setting ARF and ABB */
    if (new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) {
        // First, generate ABB by getting data from the operating registers (in the branch).
        // Suppose our branch is BEQ r3, r7 --> we will get ABB by making an or with ARF[3] and ARF[7].
        ABB = 0;
        for (i = 0; i < 16; ++i) {
            if (new_instruction.read_regs[i] == 0) { // Does not read in any more regs.
                break;
            }
            ABB = ABB | ARF[new_instruction.read_regs[i]];
        }

        // Shift all ARF entries to the left
        for (i=0; i<ARF_ROWS; ++i) {
            ARF[i] = ARF[i] << 1;
        }
    } else { // Check if this instruction writes in any register. If it does, update ARF.
        // Se escreve em registrador, a linha da ARF do RD mantem como affectors os affectors dos regs operandos
        // e marca que o branch atual eh affector (| 1).
        for (i = 0; i < 16; ++i) {
            if (new_instruction.write_regs[i] == 0) { // Does not write in any more regs.
                break;
            }
            ARF[new_instruction.write_regs[i]] = ARF[new_instruction.write_regs[i]] | 1;
        }
    }
    /* END ARF */

    /* BEGIN BTB */
    // Checa se a instrucao anterior era um branch. Se sim, atualiza o target_address dela na BTB.
    if (wasBranch) {
        // First, update GHR: create one space and, if it was a branch taken, mark LSB as 1.
        GHR = GHR << 1;
        int i, index = -1, base = getBase(previous_pc);
        for (i = 0; i < WAYS; ++i) {
            if (Btb[idx(base,i)].address == previous_pc) {
                index = i;
            }
        }
        // Btb[idx(base, index)] tells us the row in BTB to the previous instr.
        Btb[idx(base,index)].target_address = new_instruction.opcode_address;
        wasTaken = false;
        if (previous_pc + previous_instr_size != new_instruction.opcode_address) {
            // Taken
            wasTaken = true;
            GHR = GHR | 1;
            //bit2_counter += 1;
            //if (bit2_counter > 3) bit2_counter = 3;
            Btb[idx(base,index)].bht = (Btb[idx(base,index)].bht < BTB_MAX_COUNT) ? Btb[idx(base,index)].bht + 1 : BTB_MAX_COUNT;
            update_phts(getPhtIdx(GHR, ABB), getPhtIdxNf(GHR, ABB), getPhtIdx64(GHR, ABB), getPhtIdx64Nf(GHR, ABB), true);
        } else {
            // Not taken
            //bit2_counter -= 1;
            //if (bit2_counter < 0) bit2_counter = 0;
            Btb[idx(base,index)].bht = (Btb[idx(base,index)].bht <= 0) ? 0 : Btb[idx(base,index)].bht - 1;
            update_phts(getPhtIdx(GHR, ABB), getPhtIdxNf(GHR, ABB), getPhtIdx64(GHR, ABB), getPhtIdx64Nf(GHR, ABB), false);
        }
/*
        if (Btb[idx(base,index)].address + Btb[idx(base,index)].opcode_size == new_instruction.opcode_address) {
            // Se o pc atual eh o pc anterior + tam da instrucao anterior, not taken.
            Btb[idx(base,index)].bht = (Btb[idx(base,index)].bht == 0) ? 0 : Btb[idx(base,index)].bht - 1;
            update_phts(getPhtIdx(GHR, ABB), getPhtIdxNf(GHR, ABB), getPhtIdx64(GHR, ABB), getPhtIdx64Nf(GHR, ABB), true);
        } else {
            // Se nao foi nao taken, foi taken.
            Btb[idx(base,index)].bht = (Btb[idx(base,index)].bht < BTB_MAX_COUNT) ? Btb[idx(base,index)].bht + 1 : BTB_MAX_COUNT;
            update_phts(getPhtIdx(GHR, ABB), getPhtIdxNf(GHR, ABB), getPhtIdx64(GHR, ABB), getPhtIdx64Nf(GHR, ABB), false);
        }
*/
        if (predicted_2bit) {
            //if (new_instruction.opcode_address == predicted_pc_2bit) { // This if counts a hit only if I hit the address as well.
            if ((wasTaken && bit2_prediction == TAKEN) || (!wasTaken && bit2_prediction == NOT_TAKEN)) {
                ++Hit;
                // Normal ABB
                if (predicted_abb) {
                    //if (predicted_pc_2bit != predicted_pc_abb) {
                    if (bit2_prediction != abb_prediction) {
                        // In this case, what would happen is: the abb prediction would
                        // wrongly overwrite the 2 bit prevision. So we take a penalty.
                        ++WrongCorrection;
                        TotalPenalty += PENALTY_P1_R_P2_W;
                    }
                }
                if (predicted_abb_nf) {
                    // ABB Without fold-XOR
                    //if (predicted_pc_2bit != predicted_pc_abb_nf) {
                    if (bit2_prediction != abb_nf_prediction) {
                        // In this case, what would happen is: the abb prediction would
                        // wrongly overwrite the 2 bit prevision. So we take a penalty.
                        ++WrongCorrectionNf;
                        TotalPenaltyNf += PENALTY_P1_R_P2_W;
                    }
                }
                if (predicted_abb_64) {
                    // ABB 64 kb
                    //if (predicted_pc_2bit != predicted_pc_abb_64) {
                    if (bit2_prediction != abb_64_prediction) {
                        // In this case, what would happen is: the abb prediction would
                        // wrongly overwrite the 2 bit prevision. So we take a penalty.
                        ++WrongCorrection64;
                        TotalPenalty64 += PENALTY_P1_R_P2_W;
                    }
                }
                if (predicted_abb_64_nf) {
                    // ABB 64 kb without fold-XOR
                    //if (predicted_pc_2bit != predicted_pc_abb_64_nf) {
                    if (bit2_prediction != abb_64_nf_prediction) {
                        // In this case, what would happen is: the abb prediction would
                        // wrongly overwrite the 2 bit prevision. So we take a penalty.
                        ++WrongCorrection64Nf;
                        TotalPenalty64Nf += PENALTY_P1_R_P2_W;
                    }
                }

            // 2 bit missed:
            } else {
                ++Miss;
                if (predicted_abb) {
                    // Normal ABB
                    //if (predicted_pc_2bit != predicted_pc_abb) {
                    if (bit2_prediction != abb_prediction) {
                        // If 2 bit prediction was wrong BUT abb was right, penalty p1 wrong p2 right.
                        ++RightCorrection;
                        TotalPenalty += PENALTY_P1_W_P2_R;
                    } else {
                        // If 2 bit prediction was wrong AND abb was wrong, penalty p1 wrong p2 wrong.
                        TotalPenalty += PENALTY_P1_W_P2_W;
                    }
                }
                if (predicted_abb_nf) {
                    // ABB without fold-xor
                    //if (predicted_pc_2bit != predicted_pc_abb_nf) {
                    if (bit2_prediction != abb_nf_prediction) {
                        // If 2 bit prediction was wrong BUT abb was right, penalty p1 wrong p2 right.
                        ++RightCorrectionNf;
                        TotalPenaltyNf += PENALTY_P1_W_P2_R;
                    } else {
                        // If 2 bit prediction was wrong AND abb was wrong, penalty p1 wrong p2 wrong.
                        TotalPenaltyNf += PENALTY_P1_W_P2_W;
                    }
                }
                if (predicted_abb_64) {
                    // ABB 64kb
                    //if (predicted_pc_2bit != predicted_pc_abb_64) {
                    if (bit2_prediction != abb_64_prediction) {
                        // If 2 bit prediction was wrong BUT abb was right, penalty p1 wrong p2 right.
                        ++RightCorrection64;
                        TotalPenalty64 += PENALTY_P1_W_P2_R;
                    } else {
                        // If 2 bit prediction was wrong AND abb was wrong, penalty p1 wrong p2 wrong.
                        TotalPenalty64 += PENALTY_P1_W_P2_W;
                    }
                }
                if (predicted_abb_64_nf) {
                    // ABB 64kb without fold-xor
                    //if (predicted_pc_2bit != predicted_pc_abb_64_nf) {
                    if (bit2_prediction != abb_64_nf_prediction) {
                        // If 2 bit prediction was wrong BUT abb was right, penalty p1 wrong p2 right.
                        ++RightCorrection64Nf;
                        TotalPenalty64Nf += PENALTY_P1_W_P2_R;
                    } else {
                        // If 2 bit prediction was wrong AND abb was wrong, penalty p1 wrong p2 wrong.
                        TotalPenalty64Nf += PENALTY_P1_W_P2_W;
                    }
                }
            }
        }


        if (predicted_abb) {
            if ((wasTaken && abb_prediction == TAKEN) || (!wasTaken && abb_prediction == NOT_TAKEN)) {
                ++AbbWrong;
            } else {
                ++AbbRight;
            }
        }
        if (predicted_abb_nf) {
            if ((wasTaken && abb_nf_prediction == TAKEN) || (!wasTaken && abb_nf_prediction == NOT_TAKEN)) {
                ++AbbNfWrong;
            } else {
                ++AbbNfRight;
            }
        }
        if (predicted_abb_64) {
            if ((wasTaken && abb_64_prediction == TAKEN) || (!wasTaken && abb_64_prediction == NOT_TAKEN)) {
                ++Abb64Wrong;
            } else {
                ++Abb64Right;
            }
        }
        if (predicted_abb_64_nf) {
            if ((wasTaken && abb_64_nf_prediction == TAKEN) || (!wasTaken && abb_64_nf_prediction == NOT_TAKEN)) {
                ++Abb64NfWrong;
            } else {
                ++Abb64NfRight;
            }
        }


        wasBranch = false;
        previous_pc = 0;

        predicted_abb = false;
        predicted_abb_nf = false;
        predicted_abb_64 = false;
        predicted_abb_64_nf = false;
        predicted_2bit = false;

        predicted_pc_abb = 0;
        predicted_pc_abb_nf = 0;
        predicted_pc_abb_64 = 0;
        predicted_pc_abb_64_nf = 0;
        predicted_pc_2bit = 0;
    }

    // Start to gather information about this instruction to use in the next clock.
    previous_instr_size = new_instruction.opcode_size;

    if (new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) { // It is a branch
        predicted_abb = false;
        predicted_abb_nf = false;
        predicted_abb_64 = false;
        predicted_abb_64_nf = false;
        predicted_2bit = false;
        row *branchRow = inBtb(new_instruction.opcode_address);

        if (branchRow && branchRow->valid) { // We have valid information about it in our BTB
            BtbHit++;
            // Use ABB with 2k entries:
            phtIndex = getPhtIdx(GHR, ABB);
            if (predictionAbb(PHT_abb[phtIndex]) == NOT_TAKEN) {
                predicted_pc_abb = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_abb = true;
                abb_prediction = TAKEN;
            } else if (predictionAbb(PHT_abb[phtIndex]) == TAKEN) {
                predicted_pc_abb = branchRow->target_address;
                predicted_abb = true;
                abb_prediction = NOT_TAKEN;
            }

            // Use ABB with 2k entries without fold-xor:
            phtIndex = getPhtIdxNf(GHR, ABB);
            if (predictionAbb(PHT_abb_nf[phtIndex]) == NOT_TAKEN) {
                predicted_pc_abb_nf = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_abb_nf = true;
                abb_nf_prediction = TAKEN;
            } else if (predictionAbb(PHT_abb_nf[phtIndex]) == TAKEN) {
                predicted_pc_abb_nf = branchRow->target_address;
                predicted_abb_nf = true;
                abb_nf_prediction = NOT_TAKEN;
            }

            // Use ABB with 64k entries:
            phtIndex = getPhtIdx64(GHR, ABB);
            if (predictionAbb(PHT_abb_64[phtIndex]) == NOT_TAKEN) {
                predicted_pc_abb_64 = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_abb_64 = true;
                abb_64_prediction = TAKEN;
            } else if (predictionAbb(PHT_abb_64[phtIndex]) == TAKEN) {
                predicted_pc_abb_64 = branchRow->target_address;
                predicted_abb_64 = true;
                abb_64_prediction = NOT_TAKEN;
            }

            // Use ABB with 64k entries without fold-xor:
            phtIndex = getPhtIdx64Nf(GHR, ABB);
            if (predictionAbb(PHT_abb_64_nf[phtIndex]) == NOT_TAKEN) {
                predicted_pc_abb_64_nf = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_abb_64_nf = true;
                abb_64_nf_prediction = TAKEN;
            } else if (predictionAbb(PHT_abb_64_nf[phtIndex]) == TAKEN) {
                predicted_pc_abb_64_nf = branchRow->target_address;
                predicted_abb_64_nf = true;
                abb_64_nf_prediction = NOT_TAKEN;
            }
/*
            if (bit2_counter >= 2) { // Last time, branch was not taken.
                predicted_pc_2bit = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_2bit = true;
                bit2_prediction = TAKEN;
            } else if(bit2_counter <= 2) { // Last time, branch was taken.
                predicted_pc_2bit = branchRow->target_address;
                predicted_2bit = true;
                bit2_prediction = NOT_TAKEN;
            }
*/
            // Use 2 bit
            if (prediction_2bit(branchRow->bht) == NOT_TAKEN) { // Last time, branch was not taken.
                predicted_pc_2bit = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted_2bit = true;
                bit2_prediction = NOT_TAKEN;
            } else if(prediction_2bit(branchRow->bht) == TAKEN) { // Last time, branch was taken.
                predicted_pc_2bit = branchRow->target_address;
                predicted_2bit = true;
                bit2_prediction = TAKEN;
            }
        } else { // We didnt have valid information. Add a new row in our BTB.
            BtbMiss++;
            insert_row(createRow(new_instruction.opcode_address, new_instruction.opcode_size));
        }

        // Sinalize that in the next instruction we have to update target_address.
        wasBranch = true;
        previous_pc = new_instruction.opcode_address;
    }

    /* END BTB */
};

// =====================================================================
void processor_t::statistics() {
    double percentage = ((double) (100 * Hit)) / (Hit + Miss);
    double percentage_abb = ((double) (100 * AbbRight)) / (AbbRight + AbbWrong);
    double percentage_abb_nf = ((double) (100 * AbbNfRight)) / (AbbNfRight + AbbNfWrong);
    double percentage_abb_64 = ((double) (100 * Abb64Right)) / (Abb64Right + Abb64Wrong);
    double percentage_abb_64_nf = ((double) (100 * Abb64NfRight)) / (Abb64NfRight + Abb64NfWrong);

	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");
    ORCS_PRINTF("2bit Hits: %d\n", Hit);
    ORCS_PRINTF("2bit Misses: %d\n", Miss);
    ORCS_PRINTF("2bit (porcentagem acerto): %f\n", percentage);
    ORCS_PRINTF("BTB Hits: %d\n", BtbHit);
    ORCS_PRINTF("BTB Misses: %d\n", BtbMiss);
    ORCS_PRINTF("ABB Right (2k): %d\n", AbbRight);
    ORCS_PRINTF("ABB Wrong (2k): %d\n", AbbWrong);
    ORCS_PRINTF("ABB (2k) (porcentagem acerto): %f\n", percentage_abb);
    ORCS_PRINTF("ABB (2k nf) (porcentagem acerto): %f\n", percentage_abb_nf);
    ORCS_PRINTF("ABB (64k) (porcentagem acerto): %f\n", percentage_abb_64);
    ORCS_PRINTF("ABB (64k nf) (porcentagem acerto): %f\n", percentage_abb_64_nf);
    ORCS_PRINTF("ABB Right (2k nf): %d\n", AbbNfRight);
    ORCS_PRINTF("ABB Wrong (2k nf): %d\n", AbbNfWrong);
    ORCS_PRINTF("ABB Right (64k): %d\n", Abb64Right);
    ORCS_PRINTF("ABB Wrong (64k): %d\n", Abb64Wrong);
    ORCS_PRINTF("ABB Right (64k nf): %d\n", Abb64NfRight);
    ORCS_PRINTF("ABB Wrong (64k nf): %d\n", Abb64NfWrong);
    ORCS_PRINTF("Wrong Corrections (2k): %d\n", WrongCorrection);
    ORCS_PRINTF("Right Corrections (2k): %d\n", RightCorrection);
    ORCS_PRINTF("Wrong Corrections (2k nf): %d\n", WrongCorrectionNf);
    ORCS_PRINTF("Right Corrections (2k nf): %d\n", RightCorrectionNf);
    ORCS_PRINTF("Wrong Corrections (64k): %d\n", WrongCorrection64);
    ORCS_PRINTF("Right Corrections (64k): %d\n", RightCorrection64);
    ORCS_PRINTF("Wrong Corrections (64k nf): %d\n", WrongCorrection64Nf);
    ORCS_PRINTF("Right Corrections (64k nf): %d\n", RightCorrection64Nf);
    ORCS_PRINTF("Total Penalty (2k): %d\n", TotalPenalty);
    ORCS_PRINTF("Total Penalty (2k nf): %d\n", TotalPenaltyNf);
    ORCS_PRINTF("Total Penalty (64k): %d\n", TotalPenalty64);
    ORCS_PRINTF("Total Penalty (64k nf): %d\n", TotalPenalty64Nf);

/*
Hit + Miss - 100
Hit - x

100Hit = (Hit+Miss)x
x = 100 * Hit ( Hit + Miss )
*/
};
