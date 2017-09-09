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

unsigned int Hit, Miss;

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

