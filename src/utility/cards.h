/**********************************************************************
 * otpasswd -- One-time password manager and PAM module.
 * Copyright (C) 2009, 2010 by Tomasz bla Fortuna <bla@thera.be>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with otpasswd. If not, see <http://www.gnu.org/licenses/>.
 *
 * DESC:
 *   Prints ASCII or LaTeX passcards.
 **********************************************************************/

#ifndef _PASSCARDS_H_
#define _PASSCARDS_H_

#include "agent_interface.h"

/* Returns allocated memory with one passcard
 * "Number" is a passcard number. These functions 
 * add salt when needed. */
extern char *card_ascii(agent *a, const num_t number);

/* Returns allocated memory with LaTeX document with 6 passcards */
extern char *card_latex(agent *a, const num_t number);

#endif
