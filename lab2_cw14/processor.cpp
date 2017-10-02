#include "simulator.hpp"

#define PERCEPTRON 0
#define YAGS 1
#define GSHARE 2
#define ONE_BIT 3

int Predictor_type = GSHARE;

//row Btb[SETS / WAYS][WAYS];
row *Btb;
long ARF[ARF_ROWS]; // Afector Register File
long ABB; // Affector Branch Bitmap
long GHR; // Global History Register

/*
Done:
- GHR;
- Function to index PHT from GHR;
TODO:
- Get values in ARF;
- Actually set ABB;
- Pattern History Table (PHT) - second level from GHR - use GHShare, Yags or Perceptron;
*/

unsigned int Hit, Miss, BtbHit, BtbMiss, TotalPenalty;

inline int idx(int base, int deslocamento) {
    return base * WAYS + deslocamento;
}

inline int getBase(uint64_t pc) {
    return (pc >> 2) & TAG_BITS;
}

int isRegisterWritingInstr(opcode_package instr) {
    if (1 == 0)
        return 1;
    return 0;
}

row* inBtb(uint64_t pc) {
// If we find a row in Btb with this pc and valid == true, return it.
// Otherwise return NULL.
    int base = getBase(pc);
    int i, index = -1;
    for (i=0; i<WAYS; ++i) {
        if (Btb[idx(base,i)].address == pc && Btb[idx(base,i)].valid) {
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
    newRow.bht = -1;
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

int prediction1(row *branchRow, uint64_t pc) {
    /* 1 bit history */
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

int prediction2(row *branchRow) {
    /* Slower prediction to correct prediction1; */
    // ITS STILL USELESS!!
    if (branchRow->bht == 0) {
        return NOT_TAKEN;
    } else if (branchRow->bht == 1) {
        return TAKEN;
    }
    return -1;
}
/*
int predicao_pht(pht_row data) {
    // Recebe como parametro uma linha de pht, retorna TAKEN ou NAO_TAKEN.
    if (data >= 2) {
        return TAKEN;
    } else {
        return NOT_TAKEN
    }
}
*/
// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
    Btb = (row*) malloc(sizeof(struct row) * N_ROWS * WAYS);
    int i;
    for (i=0; i<N_ROWS*WAYS; ++i) {
        Btb[i].valid = false;
    }
    Hit = 0;
    Miss = 0;
    BtbHit = 0;
    BtbMiss = 0;
    TotalPenalty = 0;
    for (i=0; i<ARF_ROWS; ++i) {
        ARF[i] = 0;
    }
};

// =====================================================================
void processor_t::clock() {
    static bool wasBranch = false, predicted = false; // BTB
    static uint64_t previous_pc = 0, predicted_pc = 0; // BTB
    static int previous_instr_size;
    static bool corrected = false; // 2nd lvl prediction

	/// Get the next instruction from the trace
	opcode_package_t new_instruction;
	if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
		/// If EOF
		orcs_engine.simulator_alive = false;
        return;
	}

    /* BEGIN BTB */
    // Checa se a instrucao anterior era um branch. Se sim, atualiza o target_address dela na BTB.
    if (wasBranch) {
        // First, update GHR: create one space and, if it was a branch taken, increment 1.
        GHR << 1;
        if (previous_pc != new_instruction.opcode_address + previous_instr_size) {
            GHR += 1;
        }
        
        int i, index = 0, base = getBase(previous_pc);
        for (i=0; i<WAYS; ++i) {
            if (Btb[idx(base,i)].address == previous_pc) {
                index = i;
            }
        }
        // Btb[idx(base, index)] tells us the row in BTB to the previous instr.
        Btb[idx(base,index)].target_address = new_instruction.opcode_address;

        if (Btb[idx(base,index)].address + Btb[idx(base,index)].opcode_size == new_instruction.opcode_address) {
            // Se o pc atual eh o pc anterior + tam da instrucao anterior, not taken.
            Btb[idx(base,index)].bht = 0;
        } else {
            // Se nao foi nao taken, foi taken.
            Btb[idx(base,index)].bht = 1;
        }

        if (predicted) {
            if (new_instruction.opcode_address == predicted_pc) {
                ++Hit;
                if (corrected) {
                    ++wrongCorrection;
                    TotalPenalty += PENALTY_P1_R_P2_R;
                    corrected = false;
                } else {
                    TotalPenalty += PENALTY_P1_R_P2_W;
                }
            } else {
                ++Miss;
                if (corrected) {
                    ++correctCorrection;
                    TotalPenalty += PENALTY_P1_W_P2_R;
                    corrected = true;
                } else {
                    TotalPenalty += PENALTY_P1_W_P2_W;
                }
            }
        }

        wasBranch = false;
        previous_pc = 0;
        predicted = false;
        predicted_pc = 0;
    }

    previous_instr_size = new_instruction.opcode_size;

    /*
    Estou supondo que nao preciso efetivamente dar o rollback nem alterar o pc.
    Soh vou contar quantos eu teria acertado se eu tivesse aplicado a previsao.
    E quantos teria errado.
    Pra fazer isso, toda vez que eu tentar prever, vou marcar predicted como true
    e salvar em predicted_pc o pc que eu acredito que seria o certo. Na proxima
    instrucao eu comparo predicted_pc com a instrucao atual. Se for igual, ++hit.
    Se nao, ++miss.
    */

    if (new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) { // It is a branch
        predicted = false;
        row *branchRow = inBtb(new_instruction.opcode_address);

        if (branchRow && branchRow->valid) { // We have valid information about it in our BTB
            if (prediction1(branchRow) == NOT_TAKEN) { // Last time, branch was not taken.
                BtbHit++;
                predicted_pc = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted = true;
                if (prediction2(branchRow) == TAKEN) {
                    // Correct last misprediction.
                    corrected = true;
                }
            } else if(prediction1(branchRow) == TAKEN) { // Last time, branch was taken.
                BtbHit++;
                predicted_pc = branchRow->target_address;
                predicted = true;
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
    /* BEGIN ARF */
    /* Setting ARF and ABB */
    if (new_instruction.opcode_operation == INSTRUCTION_OPERATION_BRANCH) {
        // First, generate ABB by getting data from the 2 operating registers (in the branch).
        // Suppose our branch is BEQ r3, r7 --> we will get ABB by making an or with ARF[3] and ARF[7].
        // ABB = ARF[new_instruction.OPERANDO_1] | ARF[new_instruction.OPERANDO_2];

        // Shift all ARF entries to the left
        for (i=0; i<ARF_ROWS; ++i) {
            ARF[i] = ARF[i] << 1;
        }
    } else if (isRegisterWritingInstr(new_instruction)) {
        // Se escreve em registrador, a linha da ARF do RD mantem como affectors os affectors dos regs operandos
        // e marca que o branch atual eh affector (| 1).
        // ARF[new_instruction.DESTINO] = ARF[new_instruction.OPERANDO_1] | ARF[new_instruction.OPERANDO_2] | 1;
    }
};

// =====================================================================
void processor_t::statistics() {
	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");
    ORCS_PRINTF("Branch Hits: %d\n", Hit);
    ORCS_PRINTF("Branch Misses: %d\n", Miss);
    ORCS_PRINTF("BTB Hits: %d\n", BtbHit);
    ORCS_PRINTF("BTB Misses: %d\n", BtbMiss);
};
