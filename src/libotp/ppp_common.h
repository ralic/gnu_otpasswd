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
 **********************************************************************/

#ifndef _PPP_COMMON_H_
#define _PPP_COMMON_H_

#include <assert.h>

/* Size of fields */
#define STATE_LABEL_SIZE 30
#define STATE_CONTACT_SIZE 60
#define STATE_SPASS_SIZE 40 /* Hexadecimal SHA256 (64 bytes) of static password + SALT (16) */
#define STATE_MAX_FIELD_SIZE 80
#define STATE_ENTRY_SIZE 512 /* Maximal size of a valid state entry (single line)
			      * 32 (username) + 64 (key) + 32 (counter) + 60 (contact)
			      * + 64 (static) + 32 latest + 20 (failures + recent failures) +
			      * + 32 (timestamp) + 2 (codelength) + 5 (flags)
			      * === 343 (+ separators < 512)
			      */

#define ROWS_PER_CARD 10

/* Convert codelength to card size */
static inline int ppp_get_codes_per_row(const int codelength) {
	const int _len_to_codes[] = {
		-1, /* use up index 0, just to make it easier */
		-1, /* minimal length is 2 */
		11, /* which fits 11 passcodes in row */
		8,
		7,
		5, /* 5 - 6 */
		5,
		4, /* 7 */
		3, /* 8 - 10 */
		3,
		3,
		2, /* 11 - 16 */
		2,
		2,
		2,
		2,
		2,
	};
	assert(codelength >= 2);
	assert(codelength <= 16);
	return _len_to_codes[codelength];
}


/** We must distinguish between locking problems (critical)
 * and non-existant state file (usually not critical).
 *
 * Depending on db used and enforce option sometimes we
 * should ignore OTP login and sometimes we should hard-fail.
 */
enum ppp_errors {
	/** Warning: agent_strerror checks ranges of this values */
	STATE_NOMEM = 1000,

	/*** ALWAYS FAIL ***/
	/** Error while locking (existing) state file */
	STATE_LOCK_ERROR,

	/** Error while parsing - state invalid */
	STATE_PARSE_ERROR,

	/** Counter too big. Key should be regenerated */
	STATE_NUMSPACE,

	/** File exists, but we're unable to open/read/write
	 * state file (not a file, permissions might be wrong).
	 */
	STATE_IO_ERROR,

	/** User doesn't exists in Unix database
	 * but was required because of home directory */
	STATE_NO_SUCH_USER,

	/*** NOT ALWAYS FATAL */
	/** State doesn't exist.
	 * If enforce = 0 - ignore OTP.
	 */
	STATE_NON_EXISTENT,

	/** User home doesn't exist - not fail either
	 * but different as we are always unable to lock it */
	STATE_NO_USER_HOME,

	/** State exists, is readable, but doesn't have
	 * user entry. Always causes ignore if enforce=0
	 */
	STATE_NO_USER_ENTRY,

	/*** PPP Errors ***/

	/** Generic error. Should not happen usually. */
	PPP_ERROR = 3000,

	/** Action denied by policy */
	PPP_ERROR_POLICY,

	/** Input too long */
	PPP_ERROR_TOO_LONG,

	/** Input contains illegal characters */
	PPP_ERROR_ILL_CHAR,

	/** Value out of range */
	PPP_ERROR_RANGE,

	/** User disabled, while trying some 
	 * action like authentication */
	PPP_ERROR_DISABLED,

	/** SPass related */
	PPP_ERROR_SPASS_INCORRECT,

	/*** Errors which can happen only during initialization */

	/** Unable to read config file */
	PPP_ERROR_CONFIG,

	/** DB option in config not set. */
	PPP_ERROR_NOT_CONFIGURED,

	/** Config not owned by root */
	PPP_ERROR_CONFIG_OWNERSHIP,

	/** Incorrect config permissions
	 * Probably o+r/g+r and LDAP/MySQL selected */
	PPP_ERROR_CONFIG_PERMISSIONS,
};

enum ppp_flags {
	FLAG_SHOW = (1<<0),
	/** User disabled by administrator */
	FLAG_DISABLED = (1<<1),
	FLAG_SALTED = (1<<2),

	/* FLAG_SKIP removed */
	/* FLAG_ALPHABET_EXTENDED removed */
};


/** Warning conditions which may happen */
enum ppp_warning {
	PPP_WARN_OK = 0,		/**< No warning condition */
	PPP_WARN_LAST_CARD = 1,		/**< User on last printed card */
	PPP_WARN_NOTHING_LEFT = 2,	/**< Used up all printed passcodes */
	PPP_WARN_RECENT_FAILURES = 4,	/**< There were some failures */	
};


/** Multiple option dictionaries; static password errors and warnings */
enum ppp_multi_errors {
	PPP_ERROR_SPASS_SHORT = (1<<0),
	PPP_ERROR_SPASS_NO_DIGITS = (1<<1),
	PPP_ERROR_SPASS_NO_UPPERCASE = (1<<2),
	PPP_ERROR_SPASS_NO_SPECIAL = (1<<3),
	PPP_ERROR_SPASS_ILLEGAL_CHARACTER = (1<<4),
	PPP_ERROR_SPASS_NON_ASCII = (1<<5),

	PPP_ERROR_SPASS_POLICY = (1<<6),

	/* Password was set (for example with errors when user is privileged */
	PPP_ERROR_SPASS_SET = (1<<7),
	PPP_ERROR_SPASS_UNSET = (1<<8),
};



/* Flag-like options to some ppp functions */
enum ppp_options {
	/* Turn on policy checking */
	PPP_CHECK_POLICY = 1,

	/* Update state data in database */
	PPP_STORE = 2,

	/* Unlock state DB */
	PPP_UNLOCK = 4,

	/* Remove previously loaded user state file */
	PPP_REMOVE = 8,

	/* Do not keep lock when loading. */
	PPP_DONT_LOCK = 16,

};


/* For getters / setters. Identifies some fields in state */
enum {
	PPP_FIELD_FAILURES = 1,		/* unsigned int */
	PPP_FIELD_RECENT_FAILURES,	/* unsigned int */
	PPP_FIELD_CODE_LENGTH,		/* unsigned int */
	PPP_FIELD_ALPHABET,		/* unsigned int */
	PPP_FIELD_FLAGS,		/* unsigned int */
	PPP_FIELD_CODES_ON_CARD,	/* unsigned int */
	PPP_FIELD_CODES_IN_ROW,		/* unsigned int */
	PPP_FIELD_SPASS_SET,		/* unsigned int */

	PPP_FIELD_KEY,			/* String, getters return hexes */
	PPP_FIELD_COUNTER, 		/* num */
	PPP_FIELD_UNSALTED_COUNTER, 	/* num */
	PPP_FIELD_LATEST_CARD,		/* num */
	PPP_FIELD_CURRENT_CARD,		/* num */
	PPP_FIELD_MAX_CARD,		/* num */
	PPP_FIELD_MAX_CODE,		/* num */

	PPP_FIELD_SPASS_TIME,		/* num, state_time_t really */
	PPP_FIELD_CHANNEL_TIME,		/* num, state_time_t really */	

	PPP_FIELD_USERNAME,		/* char * */
	PPP_FIELD_PROMPT,		/* char * */
	PPP_FIELD_CONTACT,		/* char * */
	PPP_FIELD_LABEL,		/* char * */
	PPP_FIELD_SPASS,		/* char * */
};

/* Number of available alphabets */
extern const int ppp_alphabet_count;

#endif

