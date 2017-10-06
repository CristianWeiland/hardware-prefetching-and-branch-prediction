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

// ============================================================================
/// Cache Defines
// ============================================================================


/*
Endereçamento na Cache L1:

+-------------+-----------------+-----------------+
| Tag (resto) | Index (8, 6-13) | Offset (6, 0-5) |
+-------------+-----------------+-----------------+

Endereçamento na Cache L2:

+-------------+------------------+-----------------+
| Tag (resto) | Index (10, 6-13) | Offset (6, 0-5) |
+-------------+------------------+-----------------+

Linha de Cache (não inclui dados):

+-----+-------+-------+-----+
| Tag | Valid | Dirty | LRU |
+-----+-------+-------+-----+

+-----------------+----------------+----------------+
|                 |       L1       |       L2       | 
+-----------------+----------------+----------------+
| Tamanho         |           64KB |            1MB |
| Bloco           |            64B |            64B |
| Associatividade |         4 ways |         8 ways |
| Endereçamento   |           Byte |                |
| Energia         |                |                |
| Acesso          |     Sequencial |     Sequencial |
| Substituição    |   LRU Perfeito |   LRU Perfeito |
| Escrita         |     Write-Back |     Write-Back |
| Escrita Pt 2    | Write-Allocate | Write-Allocate |
| Inclusividade   |  Não Inclusiva |  Não Inclusiva |
| Ciclos Hit      |              1 |              4 |
| Ciclos Miss     |                |            200 |
+-----------------+----------------+----------------+

RAM: 4GB;
*/

#define ADDRESSING 1 // Endereçamento (bytes)
#define L1_WAYS 4 // Associatividade
#define L1_SETS 256 // Numero de conjuntos associativos da cache
#define L1_LINES 1024 // Numero de linhas da cache
#define L1_BLOCK 64 // Tamanho do bloco (bytes)
#define L1_SIZE 64*1024 // Tamanho total da Cache
#define L1_MAX_LRU L1_WAYS - 1 // Valor maximo do LRU.
#define L2_WAYS 8
// #define L2_SETS ???
#define L2_BLOCK 64
#define L2_SIZE 1024*1024

#define L1_ACCESS_TIME 1
#define L2_ACCESS_TIME 4
#define RAM_ACCESS_TIME 200

/* L1 tá certo, L2 não sei. */
#define OFFSET_L1_BITS 6
#define OFFSET_L2_BITS 6
#define PC_L1_BITS 8
#define PC_L2_BITS 10

// 0000000000011111111000000
#define PC_MASK 0xFF

struct l1_row {
    int tag;
    bool valid;
    bool dirty;
    int8_t lru;
};

typedef struct l1_row l1_row;

int idx(int base, int deslocamento);
void copy_row(row *dst, row src);
void insert_row(row *btb, row newRow);


/*
Informações úteis da instrução
isRead
isRead2
isWrite

Essas 3 servem pra ajudar em instruções CISC tipo mem[x] = mem[y] + mem[z] (LD y, LD z, ADD, SW x);

Toda vez que tiver que remover uma linha modificada (independente se tiver na L1 ou L2) eu suponho que
o Write-Back vai pra Memória Principal, então latência da RAM (RAM_ACCESS_TIME = 200).

Isso garante que nunca uma linha da L2 vai tar modificada!
*/

#endif
