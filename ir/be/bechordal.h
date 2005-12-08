
/**
 * Chordal register allocation.
 * @author Sebastian Hack
 * @date 14.12.2004
 */

#ifndef __BECHORDAL_H
#define __BECHORDAL_H

#include "irgraph.h"
#include "irnode.h"

#include "bearch.h"
#include "bera.h"

/**
 * The register allocator structure.
 */
const be_ra_t be_ra_chordal_allocator;

typedef struct _be_chordal_env_t be_chordal_env_t;

#endif
