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
 *   Set of helper functions used during PAM authentication. All are 
 *   called explicitly by pam_otpasswd.c. Used to group libotp interface
 *   into bigger tasks.
 **********************************************************************/

#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* OOB/SPASS time checks */
#include <time.h>

/* stat() */
#include <sys/stat.h>
#include <unistd.h>

/* waitpid() */
#include <sys/wait.h>

/* kill() */
#include <signal.h>

#include <pam_modules.h>

/* FreeBSD */
#include <pam_appl.h>

/* PAM declarations */
#include "pam_helpers.h"

/* libotp interface */
#include "ppp.h"

int ph_parse_module_options(int flags, int argc, const char **argv)
{
	cfg_t *cfg = cfg_get();
	assert(cfg != NULL);
	
	for (; argc-- > 0; argv++) {
		if (strcmp("audit", *argv) == 0)
			cfg->pam_logging = 2;
		else if (strcmp("debug", *argv) == 0)
			cfg->pam_logging = 3;
		else if (strcmp("silent", *argv) == 0) {
			cfg->pam_silent = CONFIG_ENABLED;
			print(PRINT_NOTICE, "pam_otpasswd silenced by PAM config parameter\n");
		} else {
			print(PRINT_ERROR, 
				"invalid PAM module parameter %s\n", *argv);
		}
	}

	if (flags & PAM_SILENT) {
		cfg->pam_silent = CONFIG_ENABLED;
		print(PRINT_NOTICE, "pam_otpasswd silenced by PAM flag\n");
	}


	return 0;
}

int ph_oob_send(pam_handle_t *pamh, state *s, const char *username)
{
	const char *oob_delay = "Not enough delay between two OTP uses.";
	int retval;
	char current_passcode[17] = {0};
	char contact[STATE_CONTACT_SIZE];
	const cfg_t *cfg = cfg_get();
	num_t time_now, time_last, time_diff;
	pid_t new_pid;
	const char *c;
	int times;
	int status = 0;

	assert(cfg != NULL);

	/* We musn't have lock on state when running this function */
	assert(ppp_is_locked(s) == 0);

	/* Check if OOB enabled */
	if (cfg->pam_oob_path == NULL || cfg->pam_oob == 0) {
		print(PRINT_WARN,
		      "trying OOB when it's not enabled; user=%s\n", username);
		return 1;
	}

	/* Check delay */
	time_now = num_i(time(NULL));
	time_last = num_i(0);
	(void) ppp_get_num(s, PPP_FIELD_CHANNEL_TIME, &time_last);
	time_diff = num_sub(time_now, time_last);
	if (num_cmp_i(time_diff, cfg->pam_oob_delay) < 0) {
		print(PRINT_WARN, "not enough delay between two OOB uses; user=%s\n", username);
		ph_show_message(pamh, oob_delay, username);
		return 1;
	}

	/* Ensure cfg->oob_path is correct */
	{
		struct stat st;
		if (stat(cfg->pam_oob_path, &st) != 0) {
			print(PRINT_ERROR,
				     "config error: unable to access oob sender. "
				     "Check oob_path parameter\n");
			return 2;
		}
		
		if (!S_ISREG(st.st_mode)) {
			print(PRINT_ERROR, "config error: oob_path is not a file!\n");
			return 2;
		}

		if ( (S_ISUID & st.st_mode) || (S_ISGID & st.st_mode) ) {
			print(PRINT_ERROR,
				    "config error: OOB utility is SUID or SGID!\n");
			return 2;
		}
		
		/* Check permissions */
		if (st.st_mode & S_IXOTH) {
			print(PRINT_WARN, 
				    "config warning: others can execute OOB utility\n");
		} else {
			/* That's cool, but can we execute it? */
			const int can_owner = 
				((st.st_mode & S_IXUSR) &&
				 st.st_uid == cfg->pam_oob_uid);
			const int can_group =
				((st.st_mode & S_IXGRP) &&
				 st.st_gid == cfg->pam_oob_gid);
			if (! (can_owner || can_group) ) {
				/* Neither from group nor from 
				 * owner mode */
				/* TODO: testcase this check */
				print(PRINT_ERROR,
					    "config error: UID %d is unable to execute "
					    "OOB utility!\n", cfg->pam_oob_uid);
				return 2;
			}
		}
	}

	/* Gather required data */
	retval = ppp_get_current(s, current_passcode);
	if (retval != 0)
		return retval;


	retval = ppp_get_str(s, PPP_FIELD_CONTACT, &c);
	if (retval != 0 || !c || strlen(c) == 0) {
		print(PRINT_WARN,
		      "user without the contact data "
		      "required for OOB transmission; user=%s\n", username);
		return 2;
	}


	/* Before doing anything invasive: update channel time */
	retval = ppp_oob_time(s);
	if (retval != 0) {
		print(PRINT_ERROR,
		      "error while updating OOB channel usage; user=%s\n", username);
		return 2;
	}

	/* Copy, as releasing state will remove this data from RAM */
	strncpy(contact, c, sizeof(contact)-1);

	new_pid = fork();
	if (new_pid == -1) {
		print(PRINT_ERROR, 
			    "unable to fork and call OOB utility\n");
		return 1;
	}

	if (new_pid == 0) {
		/* We don't want to leave state in memory! */
		retval = ppp_state_release(s, 0);
		// ppp_fini(s);
		if (retval != 0) {
			print(PRINT_ERROR, "RELEASE FAILED IN CHILD!\n");
			exit(10);
		}

		/* Drop root */
		retval = setgid(cfg->pam_oob_gid);
		if (retval != 0) {
			print_perror(PRINT_ERROR,
				     "UNABLE TO CHANGE GID TO %d\n", cfg->pam_oob_gid);
			exit(11);
		}

		retval = setuid(cfg->pam_oob_uid);
		if (retval != 0) {
			print_perror(PRINT_ERROR, 
				     "UNABLE TO CHANGE UID TO %d\n", cfg->pam_oob_uid);
			exit(12);
		}

		/* print(PRINT_NOTICE, "Managed to get to the execl (%s) with OOB.\n", cfg->oob_path); */
		execl(cfg->pam_oob_path, cfg->pam_oob_path,
		      contact, current_passcode, NULL);

		/* Whoops */
		print_perror(PRINT_ERROR, 
			     "OOB utility execve failed! Program error; "
		             "this should be detected beforehand (%s).\n",
		             strerror(errno));
		exit(13);
	}

	/*** Parent ***/
	/* Wait a bit for your child to finish.
	 * If it decides to hang up cheerfully, kill it.
	 * Then clean up the bod^C^C garbage.
	 */
	for (times = 200; times > 0; times--) {
		usleep(7000);
		retval = waitpid(new_pid, &status, WNOHANG);
		if (retval == new_pid)
			break; /* Our child finished */
		if (retval == -1) {
			print_perror(PRINT_ERROR, "waitpid failed");
			return 1;
		}
		if (retval == 0) {
			continue;
		}
	}
	if (times != 200) 
		print(PRINT_NOTICE,  "Waited 7000*%d microseconds for OOB\n", 200-times);


	if (times == 0) {
		/* Timed out while waiting for it's merry death */
		if (kill(new_pid, 9) != 0) {
			print_perror(PRINT_ERROR, "Unable to kill OOB gate.\n");
		}

		/* waitpid should return immediately now, but just wait to be sure */
		usleep(100);
		if (waitpid(new_pid, NULL, WNOHANG) != new_pid) {
			print(PRINT_ERROR, "OOB gate still running.\n");
		}
		print(PRINT_ERROR, 
			     "Timed out while waiting for OOB gate "
			     "to die. Fix it!\n");
		return 2;
	}

	print(PRINT_NOTICE, "OOB child returned fast\n");

	if (WEXITSTATUS(status) == 0)
		print(PRINT_NOTICE, "OOB utility successful\n");
	else {
		print(PRINT_WARN, 
			     "OOB utility returned %d\n", 
			     WEXITSTATUS(status));
	}

	return 0;
}

int ph_validate_spass(pam_handle_t *pamh, const state *s, const char *username)
{
	int ret = 1;
	struct pam_response *pr = NULL;

	pr = ph_query_user(pamh, 0, "Static password: ");

	if (!s) {
		/* If we don't have state we just silently fail */
		print(PRINT_NOTICE, "simulated static password query; user=%s\n", username);
		goto cleanup;
	}

	ret = ppp_spass_validate(s, pr->resp);
	if (ret != 0) {
		print(PRINT_WARN, "static password validation failed; user=%s\n", username);
	} else {
		print(PRINT_NOTICE, "static password validation succeeded; user=%s\n", username);
	}
	
cleanup:
	ph_drop_response(pr);
	return ret;
}

void ph_show_message(pam_handle_t *pamh, const char *msg, const char *username)
{
	/* Required for communication with user */
	struct pam_conv *conversation;
	struct pam_message message;
	struct pam_message *pmessage = &message;
	struct pam_response *resp = NULL;

	const cfg_t *cfg = cfg_get();

	assert(cfg != NULL);

	/* If silent enabled - don't print any messages */
	if (cfg->pam_silent == CONFIG_ENABLED) {
		print(PRINT_NOTICE, 
		      "message for user '%s' was silenced "
		      "because of configuration: %s\n", username, msg);
		return;
	}


	/* Initialize conversation function */
	/* This will generate 'dereferencing type-punned pointer' warning in GCC */
	pam_get_item(pamh, PAM_CONV, (const void **)&conversation);

	/* Set message config, and show it. */
	message.msg_style = PAM_TEXT_INFO;
	message.msg = msg;
	conversation->conv(
		1,
		(const struct pam_message**)&pmessage,
		&resp, conversation->appdata_ptr);

	print(PRINT_NOTICE, "shown user '%s' a warning: %s\n", username, msg);

	/* Drop any reply */
	if (resp)
		ph_drop_response(resp);
}

int ph_increment(pam_handle_t *pamh, const char *username, state *s)
{
	const char *enforced_msg = "OTP: Key not generated, unable to login.";
	const char *lock_msg = "OTP: Unable to lock state file.";
	const char *numspace_msg =
		"OTP: Passcode counter overflowed or state "
		"file corrupted. Regenerate key.";
	const char *invalid_msg =
		"OTP: Your state is invalid. "
		"Contact administrator.";
	const char *disabled_msg = 
		"OTP: Your state is disabled. Unable to authenticate. "
		"Contact administrator.";
	const char *policy_msg = 
		"OTP: Your state is inconsistent with "
		"system policy. Contact administrator.";

	const cfg_t *cfg = cfg_get();
	assert(cfg != NULL);

	switch (ppp_increment(s)) {
	case 0:
		/* Everything fine */
		return 0;

	case STATE_NUMSPACE:
		/* Strange error, might happen, but, huh! */
		ph_show_message(pamh, numspace_msg, username);
		print(PRINT_WARN, "user ran out of passcodes; user=%s\n", username);
		return PAM_AUTH_ERR;

	case STATE_LOCK_ERROR:
		ph_show_message(pamh, lock_msg, username);
		print(PRINT_ERROR, "lock error during auth; user=%s", username);
		return PAM_AUTH_ERR;

	case STATE_NO_USER_HOME:
	case STATE_NON_EXISTENT:
		if (cfg->pam_enforce == CONFIG_ENABLED && cfg->db == CONFIG_DB_USER)
			goto enforced_fail;

		/* Otherwise we are just not configured correctly
		 * or we are not enforcing. */
		print(PRINT_WARN, 
		      "ignoring OTP; user=%s\n", username);
		return PAM_IGNORE;

	case STATE_NO_USER_ENTRY:
		if (cfg->pam_enforce == CONFIG_ENABLED)
			goto enforced_fail;

		print(PRINT_WARN, 
		      "ignoring OTP; user=%s\n", username);

		return PAM_IGNORE;

	case PPP_ERROR_POLICY:
		print(PRINT_ERROR, "user state is inconsistent with the policy; user=%s",
		      username);
		ph_show_message(pamh, policy_msg, username);
		return PAM_AUTH_ERR;

	case PPP_ERROR_RANGE:
		print(PRINT_ERROR,
		      "user state contains invalid data; user=%s\n",
		      username);

		ph_show_message(pamh, invalid_msg, username);
		return PAM_AUTH_ERR;

	case PPP_ERROR_DISABLED:
		if (cfg->pam_enforce == CONFIG_ENABLED) {
			print(PRINT_WARN, 
			      "authentication failure; user is disabled; user=%s\n", 
			      username);

			ph_show_message(pamh, disabled_msg, username);
			return PAM_AUTH_ERR;
		} else {
			/* Not enforcing */
			print(PRINT_WARN, 
			      "ignoring OTP, user is disabled; user=%s\n", 
			      username);

			return PAM_IGNORE;
		}

	default: /* Any other problem - error */
		return PAM_AUTH_ERR;
	}

enforced_fail:
	print(PRINT_WARN, 
	      "authentication failed because of enforcement;"
	      " user=%s\n", username);
	ph_show_message(pamh, enforced_msg, username);
	return PAM_AUTH_ERR;
}

struct pam_response *ph_query_user(
	pam_handle_t *pamh, int show, const char *prompt)
{
	/* Required for communication with user */
	struct pam_conv *conversation;
	struct pam_message message;
	struct pam_message *pmessage = &message;
	struct pam_response *resp = NULL;

	/* Initialize conversation function */
	/* This will generate 'dereferencing type-punned pointer' warning in GCC */
	if (pam_get_item(pamh, PAM_CONV, (const void **)&conversation) != PAM_SUCCESS)
		return NULL;

	/* Echo on if enforced by "show" option or enabled by user
	 * and not disabled by "noshow" option
	 */
	if (show) {
		message.msg_style = PAM_PROMPT_ECHO_ON;
	} else {
		message.msg_style = PAM_PROMPT_ECHO_OFF;
	}

	message.msg = prompt;

	conversation->conv(1, (const struct pam_message **)&pmessage,
			   &resp, conversation->appdata_ptr);

	return resp;
}

void ph_drop_response(struct pam_response *reply)
{
	if (!reply)
		return;

	if (reply[0].resp) {
		char *c;
		for (c = reply[0].resp; !c; c++)
			*c = 0x00;
		free(reply[0].resp);
	}

	if (reply)
		free(reply);
}

int ph_init(pam_handle_t *pamh, int flags, int argc, const char **argv,
            state **s, const char **username)
{
	/* User info from PAM */
	const char *user = NULL;
	const cfg_t *cfg = cfg_get();

	int retval;

	assert(cfg != NULL);

	retval = ppp_init(PRINT_SYSLOG, NULL);
	if (retval != 0) {
		print(PRINT_ERROR, "OTPasswd is not correctly installed (%s)\n",
		      ppp_get_error_desc(retval));
		ppp_fini();
		return PAM_SERVICE_ERR;
	}

	/* Parse additional options passed to module */
	retval = ph_parse_module_options(flags, argc, argv);
	if (retval != 0) {
		retval = PAM_SERVICE_ERR;
		goto error;
	}

	/* Update log level with data read from module options */
	switch (cfg->pam_logging) {
	case 0: print_config(PRINT_SYSLOG | PRINT_NONE); break;
	case 1: print_config(PRINT_SYSLOG | PRINT_ERROR); break;
	case 2: print_config(PRINT_SYSLOG | PRINT_WARN); break; 
	case 3: print_config(PRINT_SYSLOG | PRINT_NOTICE); break; 
	default:
		assert(0);
		retval = PAM_SERVICE_ERR;
		goto error;
	}

	/* We must know the user of whom we must find state data */
	retval = pam_get_user(pamh, &user, NULL);
	if (retval != PAM_SUCCESS && user) {
		print(PRINT_ERROR, "pam_get_user %s", pam_strerror(pamh,retval));
		retval = PAM_USER_UNKNOWN;
		goto error;
	}

	if (user == NULL || *user == '\0') {
		print(PRINT_ERROR, "empty_username", pam_strerror(pamh,retval));
		retval = PAM_USER_UNKNOWN;
		goto error;
	}

	print(PRINT_NOTICE, "pam_otpasswd initialized; user=%s\n", user);

	/* Initialize state with given username */
	retval = ppp_state_init(s, user); 
	if (retval != 0) {
		retval = PAM_USER_UNKNOWN;
		goto error;
	}

	/* Read username back. Our local state-bound copy */
	retval = ppp_get_str(*s, PPP_FIELD_USERNAME, username);
	if (retval != 0 || !**username) {
		print(PRINT_ERROR, "internal error: Unable to"
		      " read username data from state; user=%s\n", username);
		retval = PAM_AUTH_ERR;
		goto error;
	}

	/* All ok */
	return 0;
error:
	ppp_fini();
	return retval;
}

void ph_fini(state *s)
{
	ppp_state_fini(s);
	print(PRINT_NOTICE, "pam_otpasswd finished\n");
	ppp_fini();
}
