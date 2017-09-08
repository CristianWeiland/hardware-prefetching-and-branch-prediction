#include "simulator.hpp"

//row btb[SETS / WAYS][WAYS];
row **btb;

// BTB
void copy_row(row *dst, int idx, row src) {
    dst[idx].tag = src.tag;
    dst[idx].idx = src.idx;
    dst[idx].lru = src.lru;
    dst[idx].address = src.address;
    dst[idx].valid = src.valid;
    dst[idx].bht = src.bht;
    dst[idx].target_address = src.target_address;
}

void insert_row(row *btb, row newRow) {
    int pc = row.tag;

    int base = (pc >> 2) & TAG_BITS;

    row *test;

    test = btb[0];

    int i, invalid = -1, menor_lru = WAYS+1, idx = -1;
    // Precisamos decidir quem é o cara que eu vou chutar fora.
    // Se achar alguem inválido, é ele. Se não, pega o cara com
    // menor LRU.
    for (i=0; i<WAYS; ++i) {
        if (!(btb[base][i]).valid) {
            invalid = i;
            break;
        }
        if (btb[base][i].lru < menor_lru) {
            menor_lru = btb[base][i].lru;
            idx = i;
        }
    }
    if (invalid != -1) {
        idx = i;
    }
    newRow.valid = true;
    newRow.lru = WAYS-1;
    // Nesse momento eu tenho certeza que idx contém o index certo.
    copy_row(btb[base], newRow);

    // Atualiza LRU
    for (i=0; i<WAYS; ++i) {
        --btb[base][i].lru;
    }
}

// =====================================================================
processor_t::processor_t() {

};

// =====================================================================
void processor_t::allocate() {
    btb = (row**) malloc(sizeof(row*) * N_ROWS);
    int i;
    for (i=0; i<N_ROWS; ++i) {
        btb[i] = (row*) malloc(sizeof(row) * WAYS);
    }
};

// =====================================================================
void processor_t::clock() {

	/// Get the next instruction from the trace
	opcode_package_t new_instruction;
	if (!orcs_engine.trace_reader->trace_fetch(&new_instruction)) {
		/// If EOF
		orcs_engine.simulator_alive = false;
	}

    cout << instruction_operation_t[new_instruction.opcode_operation];
};

// =====================================================================
void processor_t::statistics() {
	ORCS_PRINTF("######################################################\n");
	ORCS_PRINTF("processor_t\n");

};
