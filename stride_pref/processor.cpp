#include "simulator.hpp"

//row btb[SETS / WAYS][WAYS];
row *btb;
l1_row *L1;
// Como não temos dados, dá pra usar a mesma linha da l1.
l1_row *L2;
stride_pf *Stride_pf;

unsigned int Cycle;

unsigned int Hit, Miss, BtbHit, BtbMiss;
unsigned int L1_Hit, L1_Miss, L2_Hit, L2_Miss, Mem_Cycles;
unsigned int ST_Hit, ST_Miss, SPF_Hit, SPF_Miss, SPF_Prefetches; // SPF = Stride Prefetch
unsigned int SPF_Cycles, SPF_Delayed, UsefulPrefetches;
// SPF_Prefetches means how many times we added something through our prefetcher.
// ST_Hit means how many hits in our Stride Table, SPF_Hit means how many times we used a prefetched value.
// SPF_Cycles means how many cycles we gained by using prefetch. SPF_Delayed is how many times we prefetched
// a correct value, but not in time.

/* Caches */
inline int l1_id(int base, int deslocamento) {
    return base * L1_WAYS + deslocamento;
}
inline int l2_id(int base, int deslocamento) {
    return base * L2_WAYS + deslocamento;
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

void operate_prefetcher(uint64_t pc, uint64_t address, bool *prefetched, uint64_t *prefetched_addr) {
    // 1. Vê se esse pc tá na Stride_pf.
    int i, idx, maiorLru, invalid = -1;
    bool found = false;
    for (i=0; i<STRIDE_PF_ROWS; ++i) {
        if (Stride_pf[i].tag == pc) {
            found = true;
            idx = i;
            break;
        }
        if (Stride_pf[i].status == INVALID) {
            invalid = i;
        }
    }

    if (!found) {
        ST_Miss++;
        // It was not in our table. We have to put it in someone's place.
        maiorLru = STRIDE_PF_ROWS;
        for (i=0; i<STRIDE_PF_ROWS; ++i) {
            if (Stride_pf[i].lru > maiorLru) {
                maiorLru = Stride_pf[i].lru;
                idx = i;
            }
        }
        // Se tinha alguém inválido, usa ele. Se não, pega no menor LRU.
        if (invalid != -1) idx = invalid;

        // Adiciona a coluna nova
        Stride_pf[idx].tag = pc;
        Stride_pf[idx].status = TRAINING;
        Stride_pf[idx].stride = STRIDE_INVALID;
        Stride_pf[idx].lastAddress = address;
    } else {
        ST_Hit++;
    }

    // Atualiza LRUs
    for (i=0; i<STRIDE_PF_ROWS; ++i) Stride_pf[i].lru++;
    Stride_pf[idx].lru = 0;

    *prefetched = false;
    // Decide se vamos fazer previsão:
    if (Stride_pf[idx].status == ACTIVE) {
        // Vou prever.
        *prefetched = true;
        *prefetched_addr = address + (Stride_pf[idx].stride * STRIDE_DISTANCE);
        return;
    }



    // Senão, não vou prever; atualiza a linha do Stride_pf.
    // Se found é false, significa que eu acabei de inserir
    // essa entrada, então não preciso fazer nada.
    if (found) {
        if (Stride_pf[idx].stride == STRIDE_INVALID) {
            Stride_pf[idx].stride = address - Stride_pf[idx].lastAddress;
        } else if (pc == Stride_pf[idx].lastAddress) { // Stride ta valido, pcs batem
            Stride_pf[idx].status = ACTIVE; // Só prevê na próxima!!!
        } else { // Stride é válido, mas pcs não batem.
            Stride_pf[idx].stride = address - Stride_pf[idx].lastAddress;
        }
    }

/*
Passos pra atualizar a entrada:
1. Se não tá na Stride Table, adiciona (tag = pc, status = training, stride = invalid, lastAddress = addr).
2. Já tava na stride table mas stride == invalid: stride = addr - lastAddress;
3. Já tava na stride table, stride é valido e last address == addr: status = active
4. (Só na proxima) faz a previsão.
*/
}


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
        L1[i].tag = -1;
        L1[i].valid = false;
        L1[i].dirty = false;
        L1[i].wasPrefetched = 0;
    }
    /* Alloca Cache L2 */
    L2 = (l1_row*) malloc(sizeof(struct l1_row) * L2_LINES);
    for (i=0; i<L2_LINES; ++i) {
        L2[i].tag = -1;
        L2[i].valid = false;
        L2[i].dirty = false;
        L2[i].wasPrefetched = 0;
    }

    /* Stride Prefetcher */
    Stride_pf = (stride_pf *) malloc(STRIDE_PF_ROWS * sizeof(struct stride_pf));
    for (i=0; i<STRIDE_PF_ROWS; ++i) {
        Stride_pf[i].tag = 0;
        Stride_pf[i].lastAddress= 0;
        Stride_pf[i].stride = 0;
        Stride_pf[i].status = INVALID;
        Stride_pf[i].lru = 0;
    }

    /* Inicializa variáveis globais */
    Hit = 0;
    Miss = 0;
    BtbHit = 0;
    BtbMiss = 0;
    L1_Hit = 0;
    L2_Hit = 0;
    L1_Miss = 0;
    L2_Miss = 0;
    Mem_Cycles = 0;

    ST_Hit = 0;
    ST_Miss = 0;
    SPF_Hit = 0;
    SPF_Miss = 0;
    SPF_Cycles = 0;
    SPF_Delayed= 0;
    SPF_Prefetches = 0;
    UsefulPrefetches = 0;
};

bool in_l1(int address, bool isRead, bool *isPrefetch, unsigned int *cycle) {
    int tag = (address >> OFFSET_L1_BITS);
    int index = tag & L1_MASK; // Descobre linha da cache l1

    int i;
    for (i=0; i<L1_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (L1[i+index].tag == tag && L1[i+index].valid) {
            // If its not read, its write (duh). This means this row is dirty.
            *isPrefetch = L1[i+index].wasPrefetched;
            *cycle = L1[i+index].cycle;
            if (L1[i+index].wasPrefetched) {
                UsefulPrefetches++;
                printf("WUUUT BOYYY\n\n\n");
            }
            if (!isRead)
               L1[i+index].dirty = true;

            return true;
        }
    }
    return false;
}

bool in_l2(int address, bool isRead, bool *isPrefetch, unsigned int *cycle) {
    int tag = (address >> OFFSET_L2_BITS);
    int index = tag & L2_MASK; // Descobre linha da cache l2

    int i;
    for (i=0; i<L2_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (L2[i+index].tag == tag && L2[i+index].valid) {
            if (L2[i+index].wasPrefetched) {
                *isPrefetch = true;
                UsefulPrefetches++;
                printf("Them prefetches\n");
            }

            *cycle = L2[i+index].cycle;
            // If its not read, its write (duh). This means this row is dirty.
            if (!isRead)
                L2[i+index].dirty = true;
            return true;
        }
    }
    return false;
}

void update_lru_l1(int pc) {
    int tag = (pc >> OFFSET_L1_BITS);
    int index = tag & L1_MASK; // Descobre linha da cache l2
    int i;
    for (i=0; i<L1_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (L1[i+index].lru > 0)
            L1[i+index].lru--;
        if (L1[i+index].tag == tag) {
            L1[i+index].lru = L1_MAX_LRU;
        }
    }
}

void update_lru_l2(int pc) {
    int tag = (pc >> OFFSET_L2_BITS);
    int index = tag & L2_MASK; // Descobre linha da cache l2
    int i;
    for (i=0; i<L2_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (L2[i+index].lru > 0)
            L2[i+index].lru--;
        if (L2[i+index].tag == tag) {
            L2[i+index].lru = L2_MAX_LRU;
        }
    }
}

// Tag-index é obtido por PC >> 6. Guardar isso é suficiente. Eu poderia ignorar também o index, mas como é um simulador, então né
void add_row_cache_l1(int pc, bool isPrefetch, unsigned int cycle) {
    // Eu sei que não existe na L1. Então, só adiciona direto.
    int tag = (pc >> OFFSET_L1_BITS);
    int index = tag & L1_MASK; // Descobre linha da cache
    int invalid = -1;
    int i;

    if (isPrefetch) {
        printf("Adicionando linha de prefetch...\n");
    }

    for (i=0; i<L1_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (!L1[i+index].valid) { // Se a linha não é valida
            invalid = i;
        }
    }

    if (invalid == -1) { // Nenhuma linha inválida. Escolhe outra com LRU.
        int menor_lru = L1_MAX_LRU;
        int posicao = 0;
        for (i=0; i<L1_WAYS; ++i) {
            if (L1[index+i].lru < menor_lru) {
                menor_lru = L1[index+i].lru;
                posicao = i;
            }
        }
        // Achei posicao com menor LRU. Deixa em 'invalid'.
        invalid = posicao;
    }

    /* Ate aqui eu descobri qual linha eu vou substituir. */
    l1_row new_row;
    new_row.dirty = false;
    new_row.valid = true;
    new_row.lru = L1_MAX_LRU;
    new_row.tag = tag;
    new_row.cycle = cycle;
    new_row.wasPrefetched = isPrefetch;

    L1[index+invalid] = new_row;
/*
    // Cache nao inclusiva. Se adicionei na L1 tem que remover da L2.
    ERRADO!! ISSO EH EXCLUSIVA!! NAO FAZ ISSO!!
    tag = (pc >> OFFSET_L2_BITS);
    index = (pc >> OFFSET_L2_BITS) & L2_MASK; // Descobre linha da cache l2

    for (i=0; i<L2_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (L2[i+index].tag == tag && L2[i+index].valid) {
            L2[i+index].valid = false;
        }
    }
*/
}

void add_row_cache_l2(int pc, bool isPrefetch, unsigned int cycle) {
    // Eu sei que não existe na L1. Então, só adiciona direto.
    int tag = (pc >> OFFSET_L2_BITS);
    int index = tag & L2_MASK; // Descobre linha da cache
    int i, invalid = -1;

    for (i=0; i<L2_WAYS; ++i) { // Pra cada linha entre as N associativas
        if (!L2[i+index].valid) { // Se a linha não é valida
            invalid = i;
        }
    }

    if (invalid == -1) { // Nenhuma linha inválida. Escolhe outra com LRU.
        int posicao = 0, menor_lru = L2_MAX_LRU;
        for (i=0; i<L2_WAYS; ++i) {
            if (L2[index+i].lru < menor_lru) {
                menor_lru = L2[index+i].lru;
                posicao = i;
            }
        }
        // Achei posicao com menor LRU. Deixa em 'invalid'.
        invalid = posicao;
    }

    /* Ate aqui eu descobri qual linha eu vou substituir. */
    l1_row new_row;
    new_row.dirty = false;
    new_row.valid = true;
    new_row.lru = L2_MAX_LRU;
    new_row.tag = tag;
    new_row.cycle = cycle;
    new_row.wasPrefetched = isPrefetch;

    L2[index+invalid] = new_row;
    // if (L2[index+invalid].wasPrefetched) puts("Alala");
}

void operate_caches(bool isMemOp, uint64_t address, bool isRead, uint64_t pc) {
    if (!isMemOp) return;

    unsigned int cycleReady = Cycle; // Ciclo que vai ficar pronto o dado
    bool isPrefetch = false;
    Mem_Cycles += L1_ACCESS_TIME;
    if (in_l1(address, isRead, &isPrefetch, &cycleReady)) {
        ++L1_Hit;
        update_lru_l1(address);
        Cycle += L1_ACCESS_TIME;
    } else {
        ++L1_Miss;
        Mem_Cycles += L2_ACCESS_TIME;

        isPrefetch = false;
        cycleReady = Cycle;

        bool l2_found = in_l2(address, isRead, &isPrefetch, &cycleReady);
        if (isPrefetch) {
            printf("Achei um prefetch.\n");
        }
        if (l2_found) {
            ++L2_Hit;
            update_lru_l2(address);
            // Se eu previ e o ciclo que vai ficar pronto ainda não chegou, eu espero até
            // esse ciclo (ou seja, o ciclo atual vira aquele ciclo).
            if (isPrefetch && cycleReady > Cycle) Cycle = cycleReady;
            else Cycle += L2_ACCESS_TIME + L1_ACCESS_TIME;
        } else {
            // Operate prefetcher
            bool prefetched = false;
            uint64_t prefetchedAddress;
            operate_prefetcher(pc, address, &prefetched, &prefetchedAddress);
            // Se o prefetch quer inserir algo mas ja ta na cache l2, finge que nada aconteceu.
            if (prefetched && !in_l2(prefetchedAddress, isRead, &prefetched, &cycleReady)) {
                SPF_Prefetches++;
                add_row_cache_l2(prefetchedAddress, true, Cycle + RAM_ACCESS_TIME);
            }

            ++L2_Miss;
            Mem_Cycles += RAM_ACCESS_TIME;
            add_row_cache_l2(address, false, Cycle + RAM_ACCESS_TIME);
            Cycle += RAM_ACCESS_TIME + L2_ACCESS_TIME + L1_ACCESS_TIME;
        }
        add_row_cache_l1(address, isPrefetch, Cycle + L2_ACCESS_TIME);
    }
    // Contabiliza prefetch independentemente se foi hit na L1 ou na L2
    if (isPrefetch) SPF_Hit++;
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

    // Update L1 and L2 caches, one for each memory access.
    operate_caches(new_instruction.is_read, new_instruction.read_address, true, new_instruction.opcode_address);
    operate_caches(new_instruction.is_read2, new_instruction.read2_address, true, new_instruction.opcode_address);
    operate_caches(new_instruction.is_write, new_instruction.write_address, false, new_instruction.opcode_address);

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
    unsigned int total_l1 = L1_Hit + L1_Miss;
    unsigned int total_st = ST_Hit + ST_Miss;
    double percentage_l1 = (double(100.0 * double(L1_Hit))) / double(total_l1);
    double percentage_l2 = ((double) (100 * double(L2_Hit))) / (L2_Hit + L2_Miss);
    double percentage_st_hit = (double(100.0 * double(ST_Hit))) / double(total_st);
	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");
    /*
    ORCS_PRINTF("Branch Hits: %d\n", Hit);
    ORCS_PRINTF("Branch Misses: %d\n", Miss);
    ORCS_PRINTF("BTB Hits: %d\n", BtbHit);
    ORCS_PRINTF("BTB Misses: %d\n", BtbMiss);
    */
    ORCS_PRINTF("L1 Hits: %d\n", L1_Hit);
    ORCS_PRINTF("L1 Misses: %d\n", L1_Miss);
    ORCS_PRINTF("L1 Percentage: %lf\n", percentage_l1);
    ORCS_PRINTF("L2 Hits: %d\n", L2_Hit);
    ORCS_PRINTF("L2 Misses: %d\n", L2_Miss);
    ORCS_PRINTF("L2 Percentage: %lf\n", percentage_l2);
    ORCS_PRINTF("Mem Cycles: %d\n", Mem_Cycles);
    ORCS_PRINTF("Stride Prefetcher Statistics:");
    ORCS_PRINTF("  Stride Table Hits: %u (%lf)\n", ST_Hit, percentage_st_hit);
    ORCS_PRINTF("  Stride Table Misses: %u (%lf)\n", ST_Miss, (100.0 - percentage_st_hit));
    ORCS_PRINTF("  Stride Prefetch Hits (correct prefetches): %u\n", SPF_Hit);
    ORCS_PRINTF("  Stride Prefetch Prefetches (# of times I inserted something in l2): %u\n", SPF_Prefetches);
//unsigned int ST_Hit, ST_Miss, SPF_Hit, SPF_Miss, SPF_Prefetches; // SPF = Stride Prefetch
//unsigned int SPF_Cycles, SPF_Delayed;
};
