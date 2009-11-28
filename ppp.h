#ifndef _PPP_H_
#define _PPP_H_

#include <gmp.h>
#include "state.h"


extern int ppp_get_passcode_number(
	const state *s, const mpz_t passcard,
	mpz_t passcode, char column, char row);


extern int ppp_get_passcode(const state *s, const mpz_t counter, char *passcode);



extern void ppp_calculate(state *s);

extern void ppp_testcase(void);

#endif
