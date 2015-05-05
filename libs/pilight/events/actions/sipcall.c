/*
	Copyright (C) 2015 PieterD

	This file is part of pilight.
	
	The code in this file is based on sendmail.c.  

	pilight is free software: you can redistribute it and/or modify it under the
	terms of the GNU General Public License as published by the Free Software
	Foundation, either version 3 of the License, or (at your option) any later
	version.

	pilight is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with pilight. If not, see	<http://www.gnu.org/licenses/>
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../core/threads.h"
#include "../action.h"
#include "../../core/options.h"
#include "../../config/devices.h"
#include "../../core/dso.h"
#include "../../core/pilight.h"
#include "../../core/http.h"
#include "../../core/common.h"
#include "../../config/settings.h"
#include "../../core/log.h"
#include "sipcall.h"

#ifndef _WIN32
	#include <regex.h>
#endif

//check arguments and settings
static int checkArguments(struct rules_actions_t *obj) {
	struct JsonNode *jphonenumber = NULL;
	struct JsonNode *jtts = NULL;
	struct JsonNode *jttsfile = NULL;
    struct JsonNode *jvalues = NULL;
	struct JsonNode *jchild = NULL;
	char *stmp = NULL;
	int nrvalues = 0;

	jphonenumber = json_find_member(obj->parsedargs, "PHONENUMBER");
	jtts = json_find_member(obj->parsedargs, "TTS");
	jttsfile = json_find_member(obj->parsedargs, "TTSFILE");

	if(jphonenumber == NULL) {
		logprintf(LOG_ERR, "Sipcall action is missing a \"PHONENUMBER\"");
		return -1;
	}
	if(jtts == NULL && jttsfile == NULL) {
		logprintf(LOG_ERR, "Sipcall action is missing a \"TTS\" or \"TTSFILE\" (one is required)");
		return -1;
	}
	if(jtts != NULL && jttsfile != NULL) {
		logprintf(LOG_ERR, "Sipcall action requires \"TTS\" or \"TTSFILE\" (only one is allowed)");
		return -1;
	}
	
	nrvalues = 0;
	if((jvalues = json_find_member(jphonenumber, "value")) != NULL) {
		jchild = json_first_child(jvalues);
		while(jchild) {
			nrvalues++;
			jchild = jchild->next;
		}
	}
	if(nrvalues > 1) {
		logprintf(LOG_ERR, "Sipcall action \"PHONENUMBER\" only takes one argument");
		return -1;
	}
	
	if(jtts != NULL) {
		nrvalues = 0;
		if((jvalues = json_find_member(jtts, "value")) != NULL) {
			jchild = json_first_child(jvalues);
			while(jchild) {
				nrvalues++;
				jchild = jchild->next;
			}
		}
		if(nrvalues != 1) {
			logprintf(LOG_ERR, "Sipcall action \"TTS\" only takes one argument");
			return -1;
		}
	}

	if(jttsfile != NULL) {	
		nrvalues = 0;
		if((jvalues = json_find_member(jttsfile, "value")) != NULL) {
			jchild = json_first_child(jvalues);
			while(jchild) {
				nrvalues++;
				jchild = jchild->next;
			}
		}
		if(nrvalues != 1) {
			logprintf(LOG_ERR, "Sipcall action \"TTSFILE\" only takes one argument");
			return -1;
		}
	}
	
	// Check if mandatory settings are present in config
	if(settings_find_string("sip-program", &stmp) != EXIT_SUCCESS) {
		logprintf(LOG_ERR, "Sipcall: setting \"sip-program\" is missing in config");
		return -1;
	}	
	if(settings_find_string("sip-domain", &stmp) != EXIT_SUCCESS) {
		logprintf(LOG_ERR, "Sipcall: setting \"sip-domain\" is missing in config");
		return -1;
	}
	if(settings_find_string("sip-user", &stmp) != EXIT_SUCCESS) {
		logprintf(LOG_ERR, "Sipcall: setting \"sip-user\" is missing in config");
		return -1;
	}
	if(settings_find_string("sip-password", &stmp) != EXIT_SUCCESS) {
		logprintf(LOG_ERR, "Sipcall: setting \"sip-password\" is missing in config");
		return -1;
	}	
    if(settings_find_string("sip-ttspath", &stmp) != EXIT_SUCCESS) {
		logprintf(LOG_ERR, "Sipcall: setting \"sip-ttspath\" is missing in config");
		return -1;
	}

	return 0;
}

static void *thread(void *param) {
	struct rules_actions_t *pth = (struct rules_actions_t *)param;
    struct JsonNode *arguments = pth->parsedargs;
	struct JsonNode *jphonenumber = NULL;
	struct JsonNode *jtts = NULL;
	struct JsonNode *jttsfile = NULL;
	struct JsonNode *jvalues1 = NULL;
	struct JsonNode *jvalues2 = NULL;
	struct JsonNode *jval1 = NULL;
	struct JsonNode *jval2 = NULL;
 
	action_sipcall->nrthreads++;

	char *ssipprogram = NULL, *ssipdomain = NULL, *ssipuser = NULL, *ssippassword = NULL, *ssipttspath = NULL;
	char sipcmd[200]; 

	jphonenumber = json_find_member(arguments, "PHONENUMBER");
	jtts = json_find_member(arguments, "TTS");
	jttsfile = json_find_member(arguments, "TTSFILE");

	// read sip settings
    settings_find_string("sip-program", &ssipprogram);
	settings_find_string("sip-domain", &ssipdomain);
	settings_find_string("sip-user", &ssipuser);
	settings_find_string("sip-password", &ssippassword);
	settings_find_string("sip-ttspath", &ssipttspath);
	
	// TTS is given
	if(jphonenumber != NULL && jtts != NULL) {
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jtts, "value");

		if(jvalues1 != NULL && jvalues2 != NULL ) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {
               
				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -tts %s -ttsf %s", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, jval2->string_, ssipttspath);
				logprintf(LOG_DEBUG, sipcmd);
				if(system(sipcmd) != 0) {
					logprintf(LOG_ERR, "Sipcall failed to call \"%s\"", jval1->string_);
				}
			}
		}
	}
	// TTSFILE is given, no tts
	if(jphonenumber != NULL && jtts != NULL) {
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jttsfile, "value");

		if(jvalues1 != NULL && jvalues2 != NULL ) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {
               
				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -ttsf %s/%s", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, ssipttspath, jval2->string_ );
				logprintf(LOG_DEBUG, sipcmd);
				if(system(sipcmd) != 0) {
					logprintf(LOG_ERR, "Sipcall failed to call \"%s\"", jval1->string_);
				}
			}
		}
	}
	
		// TTS is given, use play.wav as ttsfile
	if(jphonenumber != NULL && jtts != NULL) {
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jttsfile, "value");

		if(jvalues1 != NULL && jvalues2 != NULL ) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {
               
				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -tts %s -ttsf %s/play.wav", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, jval2->string_, ssipttspath );
				logprintf(LOG_DEBUG, sipcmd);
				if(system(sipcmd) != 0) {
					logprintf(LOG_ERR, "Sipcall failed to call \"%s\"", jval1->string_);
				}
			}
		}
	}
	action_sipcall->nrthreads--;

	return (void *)NULL;
}

static int run(struct rules_actions_t *obj) {
	pthread_t pth;
	threads_create(&pth, NULL, thread, (void *)obj);
	pthread_detach(pth);
	return 0;
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void actionSipcallInit(void) {
	event_action_register(&action_sipcall, "sipcall");

	options_add(&action_sipcall->options, 'a', "PHONENUMBER", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_STRING, NULL, NULL);
	options_add(&action_sipcall->options, 'b', "TTS", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_STRING, NULL, NULL);
	options_add(&action_sipcall->options, 'b', "TTSFILE", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_STRING, NULL, NULL);

	action_sipcall->run = &run;
	action_sipcall->checkArguments = &checkArguments;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "sipcall";
	module->version = "1.0";
	module->reqversion = "6.0";
	module->reqcommit = "152";
}

void init(void) {
	actionSipcallInit();
}
#endif
