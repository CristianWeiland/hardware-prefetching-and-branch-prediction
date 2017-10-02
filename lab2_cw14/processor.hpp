#ifndef PROCESSOR_HPP
#define PROCESSOR_HPP

// ============================================================================
// ============================================================================
class processor_t {
    private:    
    
    
    public:

		// ====================================================================
		/// Methods
		// ====================================================================
		processor_t();
	    void allocate();
	    void clock();
	    void statistics();
};

#define HIT 1
#define MISS 0
#define TAKEN 1
#define NOT_TAKEN 0

// Clock penalties: they comes in 4 flavors:
// 1- P1 was right and P2 did not correct (best case);
#define PENALTY_P1_R_P2_R 0;
// 2- P1 was wrong and P2 corrected it (second best case);
#define PENALTY_P1_W_P2_R 5; // Number of cycles from fetch to finish calculating P2
// 3- P1 was wrong and P2 did not correct it (second worst case);
#define PENALTY_P1_W_P2_W 8; // This is a fixed number by our Professor.
// 4- P2 was right and P2 corrected it to wrong (worst case);
#define PENALTY_P1_R_P2_W 13;

// ============================================================================
/// BTB Defines
// ============================================================================
#define SETS 512
#define WAYS 4 // N_WAY_SET_ASSOCIATIVE
#define N_ROWS (SETS/WAYS)
#define TAG_BITS (N_ROWS - 1)

struct row {
    int tag; // 7 bits do PC (Instruction Address)
    int idx; // Possivelmente não precisa salvar na row. É o PC depois dos cálculos (>> 2 & TAG_BITS).
    int opcode_size; // Tamanho da instrucao
    uint8_t lru;
    uint64_t address; // PC Completo
    bool valid;
    int8_t bht; // Armazena a ultima direção que o salto foi tomado.
                // Se eu já tiver, eu posso prever chutando essa direção.
    uint64_t target_address; // Calculando: coloca o endereço da próxima instrução quando der o fetch da próxima.
};

typedef struct row row;

int idx(int base, int deslocamento);
void copy_row(row *dst, row src);
void insert_row(row *btb, row newRow);

// Definindo index na btb a partir do pc:
// PC shift pra direita (ignorar 2 (log(N_WAY_SET_ASSOCIATIVE)) LSB)
// 512 entradas, preciso log2(512 / 4) bits = 7.
// Pra pegar só esses bits, faz um bitwise and (&) com 127 (1111111).
// Achei a base. Agora tenho que descobrir se alguma das 4 entradas tá
// vazia. Se não tiver, joga fora a mais velha (a partir do LRU).

// Pra saber se houve um branch ou não, tenho que pegar o OPCODE_ADDRESS e somar com OPCODE_SIZE.
// Se o endereço da próxima instrução for OPCODE_ADDRESS + OPCODE_SIZE, branch not taken, se não,
// taken.

// btb = lista de conjuntos;
// conjuntos = lista de linhas;


/* FIM BTB */
/* COMEÇO ARF (Affector Register File) */
#define ARF_ROWS 32 // Number of architectural registers;

// Cada entrada da ARF eh uma sequencia de bits, portanto, um numero.
// Dependendo de quantas entradas eu quiser vou escolher qual tipo usar.
// 32 entradas = posso armazenar informacao sobre 32 branches = int
// 64 entradas = long.
// Por enquanto vou usar long.




#endif
