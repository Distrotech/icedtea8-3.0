/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
 * Copyright 2007 Red Hat, Inc.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */

#include "incls/_precompiled.incl"
#include "incls/_relocInfo_@@cpu@@.cpp.incl"

void Relocation::pd_set_data_value(address x, intptr_t o)
{
  Unimplemented();
}

address Relocation::pd_call_destination(address orig_addr)
{
  Unimplemented();
}

void Relocation::pd_set_call_destination(address x)
{
  Unimplemented();
}

address Relocation::pd_get_address_from_code()
{
  Unimplemented();
}

address* Relocation::pd_address_in_code()
{
  Unimplemented();
}

int Relocation::pd_breakpoint_size()
{
  Unimplemented();
}

void Relocation::pd_swap_in_breakpoint(address x, short* instrs,
                                       int instrlen)
{
  Unimplemented();
}

void Relocation::pd_swap_out_breakpoint(address x, short* instrs, int instrlen)
{
  Unimplemented();
}
