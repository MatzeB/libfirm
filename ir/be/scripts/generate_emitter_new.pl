#!/usr/bin/perl -w

#
# Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
#
# This file is part of libFirm.
#
# This file may be distributed and/or modified under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation and appearing in the file LICENSE.GPL included in the
# packaging of this file.
#
# Licensees holding valid libFirm Professional Edition licenses may use
# this file in accordance with the libFirm Commercial License.
# Agreement provided with the Software.
#
# This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
# WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE.
#

# This script generates C code which emits assembler code for the
# assembler ir nodes. It takes a "emit" key from the node specification
# and substitutes lines starting with . with a corresponding fprintf().
# Creation: 2005/11/07
# $Id$

use strict;
use Data::Dumper;

our $specfile;
our $target_dir;

our $arch;
our %nodes;
our %emit_templates;
our $finish_line_template = "be_emit_finish_line_gas(node);";

my $target_c = $target_dir."/gen_".$arch."_emitter.c";
my $target_h = $target_dir."/gen_".$arch."_emitter.h";

# stacks for output
my @obst_func;   # stack for the emit functions
my @obst_register;  # stack for emitter register code
my $line;

sub create_emitter {
	my $result = shift;
	my $indent = shift;
	my $template = "\\t" . shift;
	our %emit_templates;
	our $arch;

	my @tokens = ($template =~ m/(?:[^%]|%%)+|\%[a-zA-Z_][a-zA-Z0-9_]*|%\./g);
	for (@tokens) {
		SWITCH: {
			if (/%\./)       { last SWITCH; }
			if (/^%([^%]+)/) {
				if(defined($emit_templates{$1})) {
					push(@{$result}, "${indent}$emit_templates{$1}\n");
				} else {
					print "Warning: No emit_template defined for '$1'\n";
					push(@{$result}, "${indent}$1(node);\n");
				}
				last SWITCH;
			}
			$_ =~ s/%%/%/g;
			if (length($_) == 1) {
				push(@{$result}, "${indent}be_emit_char('$_');\n");
			} else {
				push(@{$result}, "${indent}be_emit_cstring(\"$_\");\n");
			}
		}
	}
	push(@{$result}, "${indent}${finish_line_template}\n");
}



foreach my $op (keys(%nodes)) {
	my %n = %{ $nodes{"$op"} };

	# skip this node description if no emit information is available
	next if (!defined($n{"emit"}));

	$line = "static void emit_${arch}_${op}(const ir_node *node)";

	push(@obst_register, "  BE_EMIT($op);\n");

	if($n{"emit"} eq "") {
		push(@obst_func, $line." {\n");
		push(@obst_func, "\t(void) node;\n");
		push(@obst_func, "}\n\n");
		next;
	}

	push(@obst_func, $line." {\n");

	my @emit = split(/\n/, $n{"emit"});

	foreach my $template (@emit) {
		# substitute only lines, starting with a '.'
		if ($template =~ /^(\s*)\.\s*(.*)/) {
			my $indent = "\t$1";
			create_emitter(\@obst_func, $indent, $2);
		} else {
			push(@obst_func, "\t$template\n");
		}
	}

	push(@obst_func, "}\n\n");
}

open(OUT, ">$target_h") || die("Could not open $target_h, reason: $!\n");

my $creation_time = localtime(time());

my $tmp = uc($arch);

print OUT<<EOF;
/**
 * \@file
 * \@brief Function prototypes for the emitter functions.
 * \@note  DO NOT EDIT THIS FILE, your changes will be lost.
 *        Edit $specfile instead.
 *        created by: $0 $specfile $target_dir
 * \@date  $creation_time
 */
#ifndef FIRM_BE_${tmp}_GEN_${tmp}_EMITTER_H
#define FIRM_BE_${tmp}_GEN_${tmp}_EMITTER_H

#include "irnode.h"
#include "${arch}_emitter.h"

void ${arch}_register_spec_emitters(void);

#endif

EOF

close(OUT);

open(OUT, ">$target_c") || die("Could not open $target_c, reason: $!\n");

$creation_time = localtime(time());

print OUT<<EOF;
/**
 * \@file
 * \@brief     Generated functions to emit code for assembler ir nodes.
 * \@note      DO NOT EDIT THIS FILE, your changes will be lost.
 *            Edit $specfile instead.
 *            created by: $0 $specfile $target_dir
 * \@date      $creation_time
 */
#include "config.h"

#include <stdio.h>

#include "irnode.h"
#include "irop_t.h"
#include "irprog_t.h"
#include "beemitter.h"

#include "gen_${arch}_emitter.h"
#include "${arch}_new_nodes.h"
#include "${arch}_emitter.h"

EOF

print OUT @obst_func;

print OUT<<EOF;
/**
 * Enters the emitter functions for handled nodes into the generic
 * pointer of an opcode.
 */
void $arch\_register_spec_emitters(void) {

#define BE_EMIT(a) op_$arch\_##a->ops.generic = (op_func)emit_$arch\_##a

  /* generated emitter functions */
EOF

print OUT @obst_register;

print OUT<<EOF;

#undef BE_EMIT
}

EOF

close(OUT);
