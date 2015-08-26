/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       Backend node support for generic backend nodes.
 * @author      Sebastian Hack
 *
 * Backend node support for generic backend nodes.
 */
#include <stdlib.h>

#include "array.h"
#include "be_t.h"
#include "bearch.h"
#include "bedump.h"
#include "beirg.h"
#include "belive.h"
#include "benode.h"
#include "besched.h"
#include "bitfiddle.h"
#include "fourcc.h"
#include "irbackedge_t.h"
#include "ircons_t.h"
#include "irgopt.h"
#include "irgwalk.h"
#include "irmode_t.h"
#include "irnode_t.h"
#include "irop_t.h"
#include "iropt_t.h"
#include "irprintf.h"
#include "irverify_t.h"
#include "obst.h"
#include "panic.h"
#include "pmap.h"
#include "raw_bitset.h"
#include "set.h"
#include "util.h"

/** The be_IncSP attribute type. */
typedef struct {
	be_node_attr_t base;
	int            offset; /**< The offset by which the stack shall be
	                            expanded/shrinked. */
	unsigned       align;  /**< alignment after the IncSP (0=no alignment) */
} be_incsp_attr_t;

typedef struct {
	be_node_attr_t base;
	ir_entity    **in_entities;
	ir_entity    **out_entities;
	int            offset;
} be_memperm_attr_t;

typedef struct be_relocation_attr_t {
	ir_entity *entity;
	unsigned   kind;
} be_relocation_attr_t;

ir_op *op_be_Asm;
ir_op *op_be_Copy;
ir_op *op_be_CopyKeep;
ir_op *op_be_IncSP;
ir_op *op_be_Keep;
ir_op *op_be_MemPerm;
ir_op *op_be_Perm;
ir_op *op_be_Relocation;
ir_op *op_be_Start;

#define be_op_tag FOURCC('B', 'E', '\0', '\0')

static int be_asm_attr_equal(ir_node const *const a, ir_node const *const b)
{
	be_asm_attr_t const *const attr_a = get_be_asm_attr_const(a);
	be_asm_attr_t const *const attr_b = get_be_asm_attr_const(b);
	return attr_a->text == attr_b->text && attr_a->operands == attr_b->operands;
}

/**
 * Compare the attributes of two be_IncSP nodes.
 */
static int be_incsp_attrs_equal(const ir_node *a, const ir_node *b)
{
	const be_incsp_attr_t *attr_a
		= (const be_incsp_attr_t*)get_irn_generic_attr_const(a);
	const be_incsp_attr_t *attr_b
		= (const be_incsp_attr_t*)get_irn_generic_attr_const(b);
	return attr_a->offset == attr_b->offset && attrs_equal_be_node(a, b);
}

static int be_relocation_attrs_equal(ir_node const *a, ir_node const *b)
{
	be_relocation_attr_t const *attr_a
		= (be_relocation_attr_t const*)get_irn_generic_attr_const(a);
	be_relocation_attr_t const *attr_b
		= (be_relocation_attr_t const*)get_irn_generic_attr_const(b);
	return attr_a->entity == attr_b->entity && attr_a->kind == attr_b->kind;
}

arch_register_req_t const **be_allocate_in_reqs(ir_graph *const irg, unsigned const n)
{
	struct obstack *const obst = be_get_be_obst(irg);
	return OALLOCN(obst, arch_register_req_t const*, n);
}

static arch_register_req_t *allocate_reg_req(ir_graph *const irg)
{
	struct obstack *obst = be_get_be_obst(irg);
	arch_register_req_t *req = OALLOCZ(obst, arch_register_req_t);
	return req;
}

static void be_node_set_register_req_in(ir_node *const node, int const pos,
                                        arch_register_req_t const *const req)
{
	backend_info_t *info = be_get_info(node);
	assert(pos < get_irn_arity(node));
	info->in_reqs[pos] = req;
}

/**
 * Initializes the generic attribute of all be nodes and return it.
 */
static void init_node_attr(ir_node *const node, unsigned const n_outputs, arch_irn_flags_t const flags)
{
	ir_graph       *irg  = get_irn_irg(node);
	backend_info_t *info = be_get_info(node);

	unsigned                    const arity   = get_irn_arity(node);
	arch_register_req_t const **const in_reqs =
		is_irn_dynamic(node) ? NEW_ARR_F(arch_register_req_t const*, arity) :
		arity != 0           ? be_allocate_in_reqs(irg, arity) :
		NULL;
	for (unsigned i = 0; i < arity; ++i) {
		in_reqs[i] = arch_no_register_req;
	}
	info->in_reqs = in_reqs;

	struct obstack *const obst = be_get_be_obst(irg);
	info->out_infos = NEW_ARR_DZ(reg_out_info_t, obst, n_outputs);
	for (unsigned i = 0; i < n_outputs; ++i) {
		info->out_infos[i].req = arch_no_register_req;
	}
	info->flags = flags;
}

ir_node *be_new_Perm(arch_register_class_t const *const cls,
                     ir_node *const block, int const n,
                     ir_node *const *const in)
{
	ir_graph *irg = get_irn_irg(block);
	ir_node  *irn = new_ir_node(NULL, irg, block, op_be_Perm, mode_T, n, in);
	init_node_attr(irn, n, arch_irn_flags_none);
	be_node_attr_t *attr = (be_node_attr_t*)get_irn_generic_attr(irn);
	attr->exc.pinned = true;
	for (int i = 0; i < n; ++i) {
		const ir_node             *input = in[i];
		const arch_register_req_t *req   = arch_get_irn_register_req(input);
		if (req->width == 1) {
			be_node_set_register_req_in(irn, i, cls->class_req);
			arch_set_irn_register_req_out(irn, i, cls->class_req);
		} else {
			arch_register_req_t *const new_req = allocate_reg_req(irg);
			new_req->cls     = cls;
			new_req->width   = req->width;
			new_req->aligned = req->aligned;
			be_node_set_register_req_in(irn, i, new_req);
			arch_set_irn_register_req_out(irn, i, new_req);
		}
	}

	return irn;
}

ir_node *be_new_MemPerm(ir_node *const block, int n, ir_node *const *const in)
{
	ir_graph *const irg = get_irn_irg(block);
	ir_node  *const irn = new_ir_node(NULL, irg, block, op_be_MemPerm, mode_T, n, in);

	init_node_attr(irn, n, arch_irn_flags_none);
	for (int i = 0; i < n; ++i) {
		be_node_set_register_req_in(  irn, i, arch_memory_req);
		arch_set_irn_register_req_out(irn, i, arch_memory_req);
	}

	be_memperm_attr_t *attr = (be_memperm_attr_t*)get_irn_generic_attr(irn);
	attr->in_entities  = OALLOCNZ(get_irg_obstack(irg), ir_entity*, n);
	attr->out_entities = OALLOCNZ(get_irg_obstack(irg), ir_entity*, n);
	attr->offset       = 0;
	return irn;
}

static void set_copy_info(ir_node *const irn, ir_graph *const irg, ir_node *const op, arch_irn_flags_t const flags)
{
	init_node_attr(irn, 1, flags);
	be_node_attr_t *const attr = (be_node_attr_t*)get_irn_generic_attr(irn);
	attr->exc.pinned = false;

	arch_register_req_t   const *const op_req = arch_get_irn_register_req(op);
	arch_register_class_t const *const cls    = op_req->cls;

	be_node_set_register_req_in(irn, 0, cls->class_req);

	arch_register_req_t *const out_req = allocate_reg_req(irg);
	out_req->cls            = cls;
	out_req->should_be_same = 1U << 0;
	out_req->aligned        = op_req->aligned;
	out_req->width          = op_req->width;
	arch_set_irn_register_req_out(irn, 0, out_req);
}

ir_node *be_new_Copy(ir_node *bl, ir_node *op)
{
	ir_graph *irg  = get_irn_irg(bl);
	ir_node  *in[] = { op };
	ir_node  *res  = new_ir_node(NULL, irg, bl, op_be_Copy, get_irn_mode(op),
	                             ARRAY_SIZE(in), in);
	set_copy_info(res, irg, op, arch_irn_flags_none);
	return res;
}

ir_node *be_get_Copy_op(const ir_node *cpy)
{
	return get_irn_n(cpy, n_be_Copy_op);
}

ir_node *be_new_Copy_before_reg(ir_node *const val, ir_node *const before, arch_register_t const *const reg)
{
	ir_node *const block = get_nodes_block(before);
	ir_node *const copy  = be_new_Copy(block, val);
	sched_add_before(before, copy);
	arch_set_irn_register_out(copy, 0, reg);
	return copy;
}

ir_node *be_new_Keep(ir_node *const block, int const n,
                     ir_node *const *const in)
{
	ir_graph *irg = get_irn_irg(block);
	ir_node  *res = new_ir_node(NULL, irg, block, op_be_Keep, mode_ANY, n, in);
	init_node_attr(res, 1, arch_irn_flag_schedule_first);
	be_node_attr_t *attr = (be_node_attr_t*) get_irn_generic_attr(res);
	attr->exc.pinned = true;

	for (int i = 0; i < n; ++i) {
		arch_register_req_t const *const req = arch_get_irn_register_req(in[i]);
		be_node_set_register_req_in(res, i, req->cls->class_req);
	}
	keep_alive(res);
	return res;
}

ir_node *be_new_Keep_one(ir_node *const kept)
{
	ir_node *const in[]  = { kept };
	ir_node *const block = get_nodes_block(kept);
	return be_new_Keep(block, ARRAY_SIZE(in), in);
}

ir_node *be_new_IncSP(const arch_register_t *sp, ir_node *bl,
                      ir_node *old_sp, int offset, unsigned align)
{
	ir_graph *irg = get_irn_irg(bl);
	ir_node  *in[] = { old_sp };
	ir_node  *irn  = new_ir_node(NULL, irg, bl, op_be_IncSP, sp->cls->mode,
	                             ARRAY_SIZE(in), in);
	init_node_attr(irn, 1, arch_irn_flags_none);
	be_incsp_attr_t *a = (be_incsp_attr_t*)get_irn_generic_attr(irn);
	a->offset          = offset;
	a->align           = align;
	a->base.exc.pinned = true;

	/* Set output constraint to stack register. */
	be_node_set_register_req_in(irn, 0, sp->cls->class_req);
	arch_copy_irn_out_info(irn, 0, old_sp);
	return irn;
}

ir_node *be_new_CopyKeep(ir_node *const bl, ir_node *const src, int const n, ir_node *const *const in_keep)
{
	ir_mode  *mode  = get_irn_mode(src);
	ir_graph *irg   = get_irn_irg(bl);
	int       arity = n+1;
	ir_node **in    = ALLOCAN(ir_node*, arity);
	in[0] = src;
	MEMCPY(&in[1], in_keep, n);
	ir_node *irn = new_ir_node(NULL, irg, bl, op_be_CopyKeep, mode, arity, in);
	set_copy_info(irn, irg, src, arch_irn_flag_schedule_first);
	for (int i = 0; i < n; ++i) {
		ir_node *pred = in_keep[i];
		arch_register_req_t const *const req = arch_get_irn_register_req(pred);
		be_node_set_register_req_in(irn, i + 1, req->cls->class_req);
	}
	return irn;
}

ir_node *be_get_CopyKeep_op(const ir_node *cpy)
{
	return get_irn_n(cpy, n_be_CopyKeep_op);
}

void be_set_MemPerm_in_entity(const ir_node *irn, unsigned n, ir_entity *ent)
{
	assert(n < be_get_MemPerm_entity_arity(irn));
	const be_memperm_attr_t *attr
		= (const be_memperm_attr_t*)get_irn_generic_attr_const(irn);
	attr->in_entities[n] = ent;
}

ir_entity* be_get_MemPerm_in_entity(const ir_node* irn, unsigned n)
{
	assert(n < be_get_MemPerm_entity_arity(irn));
	const be_memperm_attr_t *attr
		= (const be_memperm_attr_t*)get_irn_generic_attr_const(irn);
	return attr->in_entities[n];
}

void be_set_MemPerm_out_entity(const ir_node *irn, unsigned n, ir_entity *ent)
{
	assert(n < be_get_MemPerm_entity_arity(irn));
	const be_memperm_attr_t *attr
		= (const be_memperm_attr_t*)get_irn_generic_attr_const(irn);
	attr->out_entities[n] = ent;
}

ir_entity* be_get_MemPerm_out_entity(const ir_node* irn, unsigned n)
{
	assert(n < be_get_MemPerm_entity_arity(irn));
	const be_memperm_attr_t *attr
		= (const be_memperm_attr_t*)get_irn_generic_attr_const(irn);
	return attr->out_entities[n];
}

void be_set_MemPerm_offset(ir_node *irn, int offset)
{
	assert(be_is_MemPerm(irn));
	be_memperm_attr_t *attr = (be_memperm_attr_t*)get_irn_generic_attr(irn);
	attr->offset = offset;
}

int be_get_MemPerm_offset(const ir_node *irn)
{
	assert(be_is_MemPerm(irn));
	const be_memperm_attr_t *attr
		= (const be_memperm_attr_t*)get_irn_generic_attr_const(irn);
	return attr->offset;
}

unsigned be_get_MemPerm_entity_arity(const ir_node *irn)
{
	assert(be_is_MemPerm(irn));
	return get_irn_arity(irn);
}

const arch_register_req_t *be_create_reg_req(struct obstack *obst,
                                             const arch_register_t *reg,
                                             bool ignore)
{
	arch_register_class_t const *cls     = reg->cls;
	unsigned                    *limited
		= rbitset_obstack_alloc(obst, cls->n_regs);
	rbitset_set(limited, reg->index);
	arch_register_req_t *req = OALLOCZ(obst, arch_register_req_t);
	req->cls     = cls;
	req->limited = limited;
	req->width   = 1;
	req->ignore  = ignore;
	return req;
}

ir_node *be_get_IncSP_pred(ir_node *irn)
{
	assert(be_is_IncSP(irn));
	return get_irn_n(irn, 0);
}

void be_set_IncSP_pred(ir_node *incsp, ir_node *pred)
{
	assert(be_is_IncSP(incsp));
	set_irn_n(incsp, 0, pred);
}

void be_set_IncSP_offset(ir_node *irn, int offset)
{
	assert(be_is_IncSP(irn));
	be_incsp_attr_t *a = (be_incsp_attr_t*)get_irn_generic_attr(irn);
	a->offset = offset;
}

int be_get_IncSP_offset(const ir_node *irn)
{
	assert(be_is_IncSP(irn));
	const be_incsp_attr_t *a
		= (const be_incsp_attr_t*)get_irn_generic_attr_const(irn);
	return a->offset;
}

unsigned be_get_IncSP_align(const ir_node *irn)
{
	assert(be_is_IncSP(irn));
	const be_incsp_attr_t *a
		= (const be_incsp_attr_t*)get_irn_generic_attr_const(irn);
	return a->align;
}

ir_node *be_new_Phi(ir_node *block, int n_ins, ir_node **ins, ir_mode *mode,
                    const arch_register_req_t *req)
{
	ir_graph *irg  = get_irn_irg(block);
	ir_node  *phi = new_ir_node(NULL, irg, block, op_Phi, mode, n_ins, ins);
	phi->attr.phi.u.backedge = new_backedge_arr(get_irg_obstack(irg), n_ins);
	struct obstack *obst = be_get_be_obst(irg);
	backend_info_t *info = be_get_info(phi);
	info->out_infos = NEW_ARR_DZ(reg_out_info_t, obst, 1);
	info->in_reqs   = be_allocate_in_reqs(irg, n_ins);

	info->out_infos[0].req = req;
	for (int i = 0; i < n_ins; ++i) {
		info->in_reqs[i] = req;
	}
	verify_new_node(phi);
	phi = optimize_node(phi);
	return phi;
}

ir_node *be_new_Phi0(ir_node *const block, ir_mode *const mode, arch_register_req_t const *const req)
{
	ir_graph *const irg = get_irn_irg(block);
	ir_node  *const phi = new_ir_node(NULL, irg, block, op_Phi, mode, 0, NULL);
	struct obstack *const obst = be_get_be_obst(irg);
	backend_info_t *const info = be_get_info(phi);
	info->out_infos = NEW_ARR_DZ(reg_out_info_t, obst, 1);
	info->out_infos[0].req = req;
	return phi;
}

ir_node *be_complete_Phi(ir_node *const phi, unsigned const n_ins, ir_node **const ins)
{
	assert(is_Phi(phi) && get_Phi_n_preds(phi) == 0);

	ir_graph *const irg = get_irn_irg(phi);
	phi->attr.phi.u.backedge = new_backedge_arr(get_irg_obstack(irg), n_ins);
	set_irn_in(phi, n_ins, ins);

	arch_register_req_t const **const in_reqs = be_allocate_in_reqs(irg, n_ins);
	arch_register_req_t const  *const req     = arch_get_irn_register_req(phi);
	for (unsigned i = 0; i < n_ins; ++i) {
		in_reqs[i] = req;
	}
	backend_info_t *const info = be_get_info(phi);
	info->in_reqs = in_reqs;

	verify_new_node(phi);
	return optimize_node(phi);
}

void be_set_phi_reg_req(ir_node *node, const arch_register_req_t *req)
{
	assert(mode_is_data(get_irn_mode(node)));
	backend_info_t *info = be_get_info(node);
	info->out_infos[0].req = req;
	for (int i = 0, arity = get_irn_arity(node); i < arity; ++i) {
		info->in_reqs[i] = req;
	}
}

ir_node *be_new_Asm(dbg_info *const dbgi, ir_node *const block, int const n_ins, ir_node **const ins, int const n_outs, ident *const text, void *const operands)
{
	ir_graph *const irg  = get_irn_irg(block);
	ir_node  *const asmn = new_ir_node(dbgi, irg, block, op_be_Asm, mode_T, n_ins, ins);
	be_info_init_irn(asmn, arch_irn_flags_none, NULL, n_outs);

	be_asm_attr_t *const attr = (be_asm_attr_t*)get_irn_generic_attr(asmn);
	attr->text     = text;
	attr->operands = operands;

	return asmn;
}

ir_node *be_new_Relocation(ir_graph *irg, unsigned kind, ir_entity *entity)
{
	ir_node *const block = get_irg_start_block(irg);
	ir_node *const node  = new_ir_node(NULL, irg, block, op_be_Relocation,
	                                   mode_P, 0, NULL);
	be_relocation_attr_t *const attr
		= (be_relocation_attr_t*)get_irn_generic_attr(node);
	attr->entity = entity;
	attr->kind   = kind;
	ir_node *const optimized = optimize_node(node);
	return optimized;
}

unsigned be_get_Relocation_kind(ir_node const* const node)
{
	assert(be_is_Relocation(node));
	be_relocation_attr_t const *const attr
		= (be_relocation_attr_t const*)get_irn_generic_attr_const(node);
	return attr->kind;
}

ir_entity *be_get_Relocation_entity(ir_node const* const node)
{
	assert(be_is_Relocation(node));
	be_relocation_attr_t const *const attr
		= (be_relocation_attr_t const*)get_irn_generic_attr_const(node);
	return attr->entity;
}

ir_node *be_new_Start(ir_graph *const irg, be_start_out const *const outs)
{
	ir_node *const block  = get_irg_start_block(irg);
	ir_node *const start  = new_ir_node(NULL, irg, block, op_be_Start, mode_T, 0, NULL);
	unsigned const n_regs = isa_if->n_registers;

	/* Count the number of outsputs. */
	unsigned k = 1; /* +1 for memory */
	for (unsigned i = 0; i != n_regs; ++i) {
		if (outs[i] != BE_START_NO)
			++k;
	}

	be_info_init_irn(start, arch_irn_flag_schedule_first, NULL, k);

	/* Set out requirements and registers. */
	unsigned l = 0;
	arch_set_irn_register_req_out(start, l++, arch_memory_req);
	arch_register_t const *const regs = isa_if->registers;
	for (unsigned i = 0; i != n_regs; ++i) {
		if (outs[i] != BE_START_NO) {
			arch_register_t     const *const reg = &regs[i];
			arch_register_req_t const *const req = outs[i] == BE_START_IGNORE
				? be_create_reg_req(be_get_be_obst(irg), reg, true)
				: reg->single_req;
			arch_set_irn_register_req_out(start, l, req);
			arch_set_irn_register_out(    start, l, reg);
			++l;
		}
	}
	assert(l == k);

	return start;
}

ir_node *be_get_Start_mem(ir_graph *const irg)
{
	ir_node *const start = get_irg_start(irg);
	return be_get_or_make_Proj_for_pn(start, 0);
}

ir_node *be_get_Start_proj(ir_graph *const irg, arch_register_t const *const reg)
{
	ir_node *const start = get_irg_start(irg);
	/* do a naive linear search... */
	be_foreach_out(start, i) {
		arch_register_t const *const out_reg = arch_get_irn_register_out(start, i);
		if (out_reg == reg)
			return be_get_or_make_Proj_for_pn(start, i);
	}
	panic("tried querying undefined register '%s' at Start", reg->name);
}

ir_node *be_new_Proj(ir_node *const pred, unsigned const pos)
{
	arch_register_req_t const *const req = arch_get_irn_register_req_out(pred, pos);
	return new_r_Proj(pred, req->cls->mode, pos);
}

ir_node *be_new_Proj_reg(ir_node *const pred, unsigned const pos, arch_register_t const *const reg)
{
	arch_set_irn_register_out(pred, pos, reg);
	return be_new_Proj(pred, pos);
}

ir_node *be_get_or_make_Proj_for_pn(ir_node *const irn, unsigned const pn)
{
	ir_node *const proj = get_Proj_for_pn(irn, pn);
	return proj ? proj : be_new_Proj(irn, pn);
}

/**
 * ir_op-Operation: dump a be node to file
 */
static void dump_node(FILE *f, const ir_node *irn, dump_reason_t reason)
{
	assert(is_be_node(irn));

	switch (reason) {
	case dump_node_opcode_txt:
		fputs(get_irn_opname(irn), f);
		break;
	case dump_node_mode_txt: {
		ir_mode *const mode = get_irn_mode(irn);
		if (mode != mode_ANY && mode != mode_T)
			fprintf(f, "%s", get_mode_name(mode));
		break;
	}
	case dump_node_nodeattr_txt:
		if (be_is_IncSP(irn)) {
			const be_incsp_attr_t *attr
				= (const be_incsp_attr_t*)get_irn_generic_attr_const(irn);
			fprintf(f, " [%d] ", attr->offset);
		}
		break;
	case dump_node_info_txt:
		if (be_is_IncSP(irn)) {
			const be_incsp_attr_t *a
				= (const be_incsp_attr_t*)get_irn_generic_attr_const(irn);
			fprintf(f, "align: %u\n", a->align);
			fprintf(f, "offset: %d\n", a->offset);
		} else if (be_is_MemPerm(irn)) {
			for (unsigned i = 0; i < be_get_MemPerm_entity_arity(irn); ++i) {
				ir_entity *in  = be_get_MemPerm_in_entity(irn, i);
				ir_entity *out = be_get_MemPerm_out_entity(irn, i);
				if (in != NULL)
					fprintf(f, "\nin[%u]: %s\n", i, get_entity_name(in));
				if (out != NULL)
					fprintf(f, "\nout[%u]: %s\n", i, get_entity_name(out));
			}
		}
		break;
	}
}

void be_copy_attr(ir_graph *const irg, ir_node const *const old_node, ir_node *const new_node)
{
	void const *const old_attr = get_irn_generic_attr_const(old_node);
	void       *const new_attr = get_irn_generic_attr(new_node);
	memcpy(new_attr, old_attr, get_op_attr_size(get_irn_op(old_node)));

	backend_info_t *const old_info = be_get_info(old_node);
	backend_info_t *const new_info = be_get_info(new_node);
	*new_info = *old_info;
	memset(&new_info->sched_info, 0, sizeof(new_info->sched_info));
	if (new_info->out_infos) {
		struct obstack *const obst = be_get_be_obst(irg);
		new_info->out_infos = DUP_ARR_D(reg_out_info_t, obst, new_info->out_infos);
	}
}

bool is_be_node(const ir_node *irn)
{
	return get_op_tag(get_irn_op(irn)) == be_op_tag;
}

static ir_op *new_be_op(unsigned code, const char *name, op_pin_state p,
                        irop_flags flags, op_arity opar, size_t attr_size)
{
	ir_op *res = new_ir_op(code, name, p, flags, opar, 0, attr_size);
	set_op_dump(res, dump_node);
	set_op_copy_attr(res, be_copy_attr);
	set_op_tag(res, be_op_tag);
	return res;
}

void be_init_op(void)
{
	assert(op_be_Perm == NULL);

	/* Acquire all needed opcodes. */
	unsigned const o = get_next_ir_opcodes(beo_last + 1);
	op_be_Asm        = new_be_op(o+beo_Asm,        "be_Asm",        op_pin_state_exc_pinned, irop_flag_none,                            oparity_any,      sizeof(be_asm_attr_t));
	op_be_Copy       = new_be_op(o+beo_Copy,       "be_Copy",       op_pin_state_exc_pinned, irop_flag_none,                            oparity_any,      sizeof(be_node_attr_t));
	op_be_CopyKeep   = new_be_op(o+beo_CopyKeep,   "be_CopyKeep",   op_pin_state_exc_pinned, irop_flag_keep,                            oparity_variable, sizeof(be_node_attr_t));
	op_be_IncSP      = new_be_op(o+beo_IncSP,      "be_IncSP",      op_pin_state_exc_pinned, irop_flag_none,                            oparity_any,      sizeof(be_incsp_attr_t));
	op_be_Keep       = new_be_op(o+beo_Keep,       "be_Keep",       op_pin_state_exc_pinned, irop_flag_keep,                            oparity_variable, sizeof(be_node_attr_t));
	op_be_MemPerm    = new_be_op(o+beo_MemPerm,    "be_MemPerm",    op_pin_state_exc_pinned, irop_flag_none,                            oparity_variable, sizeof(be_memperm_attr_t));
	op_be_Perm       = new_be_op(o+beo_Perm,       "be_Perm",       op_pin_state_exc_pinned, irop_flag_none,                            oparity_variable, sizeof(be_node_attr_t));
	op_be_Relocation = new_be_op(o+beo_Relocation, "be_Relocation", op_pin_state_floats,     irop_flag_constlike|irop_flag_start_block, oparity_any,      sizeof(be_relocation_attr_t));
	op_be_Start      = new_be_op(o+beo_Start,      "be_Start",      op_pin_state_pinned,     irop_flag_start_block,                     oparity_variable, sizeof(be_node_attr_t));

	set_op_attrs_equal(op_be_Asm,        be_asm_attr_equal);
	set_op_attrs_equal(op_be_Copy,       attrs_equal_be_node);
	set_op_attrs_equal(op_be_CopyKeep,   attrs_equal_be_node);
	set_op_attrs_equal(op_be_IncSP,      be_incsp_attrs_equal);
	set_op_attrs_equal(op_be_Keep,       attrs_equal_be_node);
	set_op_attrs_equal(op_be_MemPerm,    attrs_equal_be_node);
	set_op_attrs_equal(op_be_Perm,       attrs_equal_be_node);
	set_op_attrs_equal(op_be_Relocation, be_relocation_attrs_equal);
}

void be_finish_op(void)
{
	free_ir_op(op_be_Copy);     op_be_Copy     = NULL;
	free_ir_op(op_be_CopyKeep); op_be_CopyKeep = NULL;
	free_ir_op(op_be_IncSP);    op_be_IncSP    = NULL;
	free_ir_op(op_be_Keep);     op_be_Keep     = NULL;
	free_ir_op(op_be_MemPerm);  op_be_MemPerm  = NULL;
	free_ir_op(op_be_Perm);     op_be_Perm     = NULL;
}
