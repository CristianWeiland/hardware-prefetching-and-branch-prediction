#include "simulator.hpp"

//row btb[SETS / WAYS][WAYS];
row *btb;

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
        if (btb[idx(base,i)].address == pc && btb[idx(base,i)].valid) {
            index = i;
        }
    }
    if (index > 1) {
        return &(btb[idx(base, index)]);
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
        if (!(btb[idx(base, i)]).valid) {
            invalid = i;
            break;
        }
        if (btb[idx(base,i)].lru < menor_lru) {
            menor_lru = btb[idx(base,i)].lru;
            index = i;
        }
    }
    if (invalid != -1) {
        index = invalid;
    }
    newRow.valid = true;
    newRow.lru = WAYS-1;
    // Nesse momento eu tenho certeza que idx contém o index certo.
    copy_row(&(btb[idx(base, index)]), newRow);

    // Atualiza LRUs
    for (i=0; i<WAYS; ++i) {
        --btb[idx(base,i)].lru;
    }
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
    btb = (row*) malloc(sizeof(struct row) * N_ROWS * WAYS);
    int i;
    for (i=0; i<N_ROWS*WAYS; ++i) {
        btb[i].valid = false;
    }
    // TODO: Fill BTB with invalid values.
    Hit = 0;
    Miss = 0;
};

// =====================================================================
void processor_t::clock() {
    static bool wasBranch = false, predicted = false;
    static uint64_t previous_pc = 0, predicted_pc = 0;

	/// Get the next instruction from the trace
	opcode_package_t new_instruction;
	if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
		/// If EOF
		orcs_engine.simulator_alive = false;
        return;
	}

    // Checa se a instrucao anterior era um branch. Se sim, atualiza o target_address dela na BTB.
    if (wasBranch) {
        int i, index = 0, base = getBase(previous_pc);
        for (i=0; i<WAYS; ++i) {
            if (btb[idx(base,i)].address == previous_pc) {
                index = i;
            }
        }
        // btb[idx(base, index)] tells us the row in BTB to the previous instr.
        btb[idx(base,index)].target_address = new_instruction.opcode_address;

        if (btb[idx(base,index)].address + btb[idx(base,index)].opcode_size == new_instruction.opcode_address) {
            // Se o pc atual eh o pc anterior + tam da instrucao anterior, not taken.
            btb[idx(base,index)].bht = 0;
        } else {
            // Se nao foi nao taken, foi taken.
            btb[idx(base,index)].bht = 1;
        }

        if (predicted) {
            if (new_instruction.opcode_address == predicted_pc) {
                ++Hit;
            } else {
                ++Miss;
            }
        }

        wasBranch = false;
        previous_pc = 0;
        predicted = false;
        predicted_pc = 0;
    }

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
            if (branchRow->bht == 0) { // Last time, branch was not taken.
                predicted_pc = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted = true;
            } else if(branchRow->bht == 1) { // Last time, branch was taken.
                if (branchRow->target_address != 0) { // Branch was taken. Do we have an address to go?
                    predicted_pc = branchRow->target_address;
                    predicted = true;
                }
            }
        } else { // We didnt have valid information. Add a new row in our BTB.
            insert_row(createRow(new_instruction.opcode_address, new_instruction.opcode_size));
        }

        // Sinalize that in the next instruction we have to update target_address.
        wasBranch = true;
        previous_pc = new_instruction.opcode_address;
    }
};

// =====================================================================
void processor_t::statistics() {
	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");
    ORCS_PRINTF("Branch Hits: %d\n", Hit);
    ORCS_PRINTF("Branch Misses: %d\n", Miss);
};
