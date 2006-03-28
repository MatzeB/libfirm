#ifndef _BEARCH_IA32_T_H_
#define _BEARCH_IA32_T_H_

#include "firm_config.h"

#include "debug.h"
#include "bearch_ia32.h"
#include "ia32_nodes_attr.h"
#include "set.h"
#include "../be.h"

#ifdef NDEBUG
#define SET_IA32_ORIG_NODE(n, o)
#else
#define SET_IA32_ORIG_NODE(n, o) set_ia32_orig_node(n, o);
#endif /* NDEBUG */

/* some typedefs */

/**
 * Bitmask for the backend optimization settings.
 */
typedef struct _ia32_optimize_t {
	unsigned incdec    : 1;   /**< optimize add/sub 1/-1 to inc/dec */
	unsigned doam      : 1;   /**< do address mode optimizations */
	unsigned placecnst : 1;   /**< place constants in the blocks where they are used */
	unsigned immops    : 1;   /**< create operations with immediates */
	unsigned extbb     : 1;   /**< do extended basic block scheduling */
} ia32_optimize_t;

/** floating point support */
typedef enum fp_support {
  fp_x87,   /**< use x87 instructions */
  fp_sse2   /**< use SSE2 instructions */
} fp_support;

/**
 * IA32 code generator
 */
typedef struct _ia32_code_gen_t {
	const arch_code_generator_if_t *impl;          /**< implementation */
	ir_graph                       *irg;           /**< current irg */
	FILE                           *out;           /**< output file */
	const arch_env_t               *arch_env;      /**< the arch env */
	set                            *reg_set;       /**< set to memorize registers for non-ia32 nodes (e.g. phi nodes) */
	firm_dbg_module_t              *mod;           /**< debugging module */
	int                             emit_decls;    /**< flag indicating if decls were already emitted */
	pmap                           *types;         /**< A map of modes to primitive types */
	pmap                           *tv_ent;        /**< A map of entities that store tarvals */
	const be_irg_t                 *birg;          /**< The be-irg (contains additional information about the irg) */
	ir_node                        **blk_sched;    /**< an array containing the scheduled blocks */
	ia32_optimize_t                 opt;           /**< contains optimization information */
	char                            fp_kind;       /**< floating point kind */
} ia32_code_gen_t;

typedef struct _ia32_isa_t {
	const arch_isa_if_t   *impl;
	const arch_register_t *sp;            /**< The stack pointer register. */
	const arch_register_t *bp;            /**< The base pointer register. */
	const int              stack_dir;     /**< -1 for decreasing, 1 for increasing. */
	int                    num_codegens;  /**< The number of code generator objects created so far */
	pmap                  *regs_16bit;    /**< Contains the 16bits names of the gp registers */
	pmap                  *regs_8bit;     /**< Contains the 8bits names of the gp registers */
	char                   fp_kind;       /**< floating point kind */
#ifndef NDEBUG
	struct obstack        *name_obst;     /**< holds the original node names (for debugging) */
	unsigned long          name_obst_size;
#endif /* NDEBUG */
} ia32_isa_t;

typedef struct _ia32_irn_ops_t {
	const arch_irn_ops_if_t *impl;
	ia32_code_gen_t         *cg;
} ia32_irn_ops_t;

/* this is a struct to minimize the number of parameters
   for transformation walker */
typedef struct _ia32_transform_env_t {
	firm_dbg_module_t *mod;        /**< The firm debugger */
	dbg_info          *dbg;        /**< The node debug info */
	ir_graph          *irg;        /**< The irg, the node should be created in */
	ir_node           *block;      /**< The block, the node should belong to */
	ir_node           *irn;        /**< The irn, to be transformed */
	ir_mode           *mode;       /**< The mode of the irn */
	ia32_code_gen_t   *cg;         /**< The code generator */
} ia32_transform_env_t;

/**
 * Creates the unique per irg GP NoReg node.
 */
ir_node *ia32_new_NoReg_gp(ia32_code_gen_t *cg);

/**
 * Creates the unique per irg FP NoReg node.
 */
ir_node *ia32_new_NoReg_fp(ia32_code_gen_t *cg);

#endif /* _BEARCH_IA32_T_H_ */
