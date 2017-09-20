#include "simulator.hpp"

//row btb[SETS / WAYS][WAYS];
row *btb;
l1_row L1;

unsigned int Hit, Miss, BtbHit, BtbMiss;
unsigned int L1_Hit, L1_Miss;

/* Caches */
inline int l1_id(int base, int deslocamento) {
    return base * L1_WAYS + deslocamento;
}

/* BTB Start */

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

/* BTB End */


// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
    /* Alloca BTB */
    btb = (row*) malloc(sizeof(struct row) * N_ROWS * WAYS);
    int i;
    for (i=0; i<N_ROWS*WAYS; ++i) {
        btb[i].valid = false;
    }
    /* Alloca Cache L1 */
    L1 = (l1_row*) malloc(sizeof(struct l1_row) * L1_LINES);
    for (i=0; i<L1_LINES; ++i) {
        L1[i].valid = false;
        L1[i].dirty= false;
    }
    /* Inicializa variáveis globais */
    Hit = 0;
    Miss = 0;
    BtbHit = 0;
    BtbMiss = 0;
};

bool in_l1() {
    // Descobre linha da cache (decodificando PC??)
    // (i) Para cada linha entre as N associativas:
    //     Se tags forem iguais:
    //         (j) Para cada linha entre as N associativas:
    //             linha[j].lru--;
    //         linha[i].lru = L1_MAX_LRU;
    //         return true;
    //     Else
    //         return false;
    return true;
}

void add_row_cache(l1_row new_row) {
    // Implementado pensando na L1, tem que pensar mais pra L2.
    return;
    // Eu sei que não existe na L1. Então, só adiciona direto.
    int index = -999; // Descobre linha da cache (decodificando PC??)
    int invalid = -1;
    int i;
    for (i=0; i<L1_WAYS; ++i) { Pra cada linha entre as N associativas
        if (!l1[i+index].valid) { Se a linha não é valida
            invalid = i;
        }
    }

    if (invalid == -1) { // Nenhuma linha inválida. Escolhe outra com LRU.
        int menor_lru = L1_MAX_LRU;
        int posicao = 0;
        for (i=0; i<L1_WAYS; ++i) {
            if (l1[index+i].lru < menor_lru) {
                menor_lru = l1[index+i].lru;
                posicao = i;
            }
        }
        // Achei posicao com menor LRU. Deixa em 'invalid'.
        invalid = posicao;
    }

    l1[index+invalid] = new_row;
}

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

    // TODO: Criar l1_row.
    // TODO: Verificar latencia / energia.
    // TODO: Transformar CISC em microOps.
    // Cache:
    if (in_l1(new_instruction)) {
        ++L1_Hit;
    } else {
        ++L1_Miss;
        // add_row_cache();
    }

    // BTB:
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
                BtbHit++;
                predicted_pc = new_instruction.opcode_address + new_instruction.opcode_size;
                predicted = true;
            } else if(branchRow->bht == 1) { // Last time, branch was taken.
                if (branchRow->target_address != 0) { // Branch was taken. Do we have an address to go?
                    BtbHit++;
                    predicted_pc = branchRow->target_address;
                    predicted = true;
                } else {
                    printf("This should NOT have happened...\n");
                }
            }
        } else { // We didnt have valid information. Add a new row in our BTB.
            BtbMiss++;
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
    ORCS_PRINTF("BTB Hits: %d\n", BtbHit);
    ORCS_PRINTF("BTB Misses: %d\n", BtbMiss);
};
