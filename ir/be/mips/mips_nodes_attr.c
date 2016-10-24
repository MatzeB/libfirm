/*
 * This file is part of libFirm.
 * Copyright (C) 2016 University of Karlsruhe.
 */

#include <panic.h>

#include "mips_nodes_attr.h"

char const *mips_get_cond_name(mips_cond_t const cond)
{
	switch (cond) {
	case mips_cc_eq:  return "eq";
	case mips_cc_ne:  return "ne";
	}
	panic("invalid cond");
}
