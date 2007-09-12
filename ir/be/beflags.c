/*
 * Copyright (C) 1995-2007 University of Karlsruhe.  All right reserved.
 *
 * This file is part of libFirm.
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * Licensees holding valid libFirm Professional Edition licenses may use
 * this file in accordance with the libFirm Commercial License.
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

/**
 * @file
 * @brief       modifies schedule so flags dependencies are respected.
 * @author      Matthias Braun, Christoph Mallon
 * @version     $Id: besched.h 14693 2007-06-21 15:35:49Z beck $
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "irgwalk.h"
#include "irnode_t.h"
#include "irtools.h"
#include "ircons.h"
#include "iredges_t.h"
#include "irprintf.h"
#include "error.h"

#include "beflags.h"
#include "bearch_t.h"
#include "beirg_t.h"
#include "besched_t.h"

static const arch_env_t            *arch_env   = NULL;
static const arch_register_class_t *flag_class = NULL;
static const arch_register_t       *flags_reg  = NULL;
static func_rematerialize           remat      = NULL;

static ir_node *default_remat(ir_node *node, ir_node *after)
{
	ir_node *copy  = exact_copy(node);
	sched_add_after(after, copy);

	return copy;
}

/**
 * tests wether we can legally move node node after node after
 * (only works for nodes in same block)
 */
static int can_move(ir_node *node, ir_node *after)
{
	const ir_edge_t *edge;
	assert(get_nodes_block(node) == get_nodes_block(after));

	/* TODO respect dep edges */
	assert(get_irn_n_edges_kind(node, EDGE_KIND_DEP) == 0);

	/** all users have to be after the after node */
	foreach_out_edge(node, edge) {
		ir_node *out = get_edge_src_irn(edge);
		if(is_Proj(out)) {
			const ir_edge_t *edge2;
			assert(get_irn_n_edges_kind(out, EDGE_KIND_DEP) == 0);
			foreach_out_edge(out, edge2) {
				ir_node *out2 = get_edge_src_irn(edge2);
				/* phi represents a usage at block end */
				if(is_Phi(out2))
					continue;
				if(sched_get_time_step(out2) <= sched_get_time_step(after)) {
					return 0;
				}
			}
		} else {
			/* phi represents a usage at block end */
			if(is_Phi(out))
				continue;
			if(sched_get_time_step(out) <= sched_get_time_step(after)) {
				return 0;
			}
		}
	}

	ir_fprintf(stderr, "Can move node %+F after node %+F\n", node, after);
	return 1;
}

static void rematerialize_or_move(ir_node *flags_needed, ir_node *node,
                                  ir_node *flag_consumers, int pn)
{
	ir_node *n;
	ir_node *copy;
	ir_node *value;

	if(can_move(flags_needed, node)) {
		/* move it */
		sched_remove(flags_needed);
		sched_add_after(node, flags_needed);
		return;
	}

	copy = remat(flags_needed, node);

	if(get_irn_mode(copy) == mode_T) {
		ir_node *block = get_nodes_block(copy);
		ir_mode *mode  = flag_class->mode;
		value = new_rd_Proj(NULL, current_ir_graph, block,
							copy, mode, pn);
	} else {
		value = copy;
	}

	n = flag_consumers;
	do {
		int i;
		int arity = get_irn_arity(n);
		for(i = 0; i < arity; ++i) {
			ir_node *in = get_irn_n(n, i);
			in = skip_Proj(in);
			if(in == flags_needed) {
				set_irn_n(n, i, value);
				break;
			}
		}
		n = get_irn_link(n);
	} while(n != NULL);
}

static void fix_flags_walker(ir_node *block, void *env)
{
	ir_node *node;
	ir_node *flags_needed   = NULL;
	ir_node *flag_consumers = NULL;
	int      pn;
	(void) env;

	sched_foreach_reverse(block, node) {
		int i, arity;
		ir_node *new_flags_needed = NULL;

		if(node == flags_needed) {
			/* all ok */
			flags_needed   = NULL;
			flag_consumers = NULL;
		}

		/* test wether node destroys the flags */
		if(flags_needed != NULL && arch_irn_is(arch_env, node, modify_flags)) {
			/* rematerialize */
			rematerialize_or_move(flags_needed, node, flag_consumers, pn);
			flags_needed   = NULL;
			flag_consumers = NULL;
		}

		/* test wether the current node needs flags */
		arity = get_irn_arity(node);
		for(i = 0; i < arity; ++i) {
			//ir_node *in = get_irn_n(node, i);
			const arch_register_class_t *cls
				= arch_get_irn_reg_class(arch_env, node, i);
			if(cls == flag_class) {
				assert(new_flags_needed == NULL);
				new_flags_needed = get_irn_n(node, i);
			}
		}

		if(new_flags_needed == NULL)
			continue;

		if(new_flags_needed != flags_needed) {
			if(flags_needed != NULL) {
				/* rematerialize node */
				rematerialize_or_move(flags_needed, node, flag_consumers, pn);
				flags_needed    = NULL;
				flag_consumers = NULL;
			}

			if(get_nodes_block(new_flags_needed) != block) {
				panic("remat across blocks not implemented yet");
				flags_needed   = NULL;
				flag_consumers = NULL;
			} else {
				flags_needed = new_flags_needed;
				arch_set_irn_register(arch_env, flags_needed, flags_reg);
				if(is_Proj(flags_needed)) {
					pn           = get_Proj_proj(flags_needed);
					flags_needed = get_Proj_pred(flags_needed);
				}
				flag_consumers = node;
				set_irn_link(flag_consumers, NULL);
				assert(arch_irn_is(arch_env, flags_needed, rematerializable));
			}
		} else {
			/* link all consumers in a list */
			set_irn_link(flag_consumers, node);
			flag_consumers = node;
		}
	}

	assert(flags_needed   == NULL);
	assert(flag_consumers == NULL);
}

void be_sched_fix_flags(be_irg_t *birg, const arch_register_class_t *flag_cls,
                        func_rematerialize remat_func)
{
	ir_graph *irg = be_get_birg_irg(birg);

	arch_env   = be_get_birg_arch_env(birg);
	flag_class = flag_cls;
	flags_reg  = & flag_class->regs[0];
	remat      = remat_func;
	if(remat == NULL)
		remat = &default_remat;

	set_using_irn_link(irg);
	irg_block_walk_graph(irg, fix_flags_walker, NULL, NULL);
	clear_using_irn_link(irg);
}
