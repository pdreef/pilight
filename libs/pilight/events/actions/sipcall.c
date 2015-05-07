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

/*
=================================================================================
Name        : sipcall.c
Version     : 0.1 alpha

Copyright (C) 2012 by Andre Wussow, 2012, desk@binerry.de

Description :
Tool for making automated calls over SIP/VOIP with PJSUA library and eSpeak.

Dependencies:
- PJSUA API (PJSIP)
- eSpeak

References  :
http://www.pjsip.org/
http://www.pjsip.org/docs/latest/pjsip/docs/html/group__PJSUA__LIB.htm
http://espeak.sourceforge.net/
http://binerry.de/post/29180946733/raspberry-pi-caller-and-answering-machine

================================================================================
This tool is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This tool is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
================================================================================
*/

// definition of endianess (e.g. needed on raspberry pi)
#define PJ_IS_LITTLE_ENDIAN 1
#define PJ_IS_BIG_ENDIAN 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <signal.h>
#include <pjsua-lib/pjsua.h>

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


// some espeak options
#define ESPEAK_LANGUAGE "nl"
#define ESPEAK_AMPLITUDE 100
#define ESPEAK_CAPITALS_PITCH 20
#define ESPEAK_SPEED 150
#define ESPEAK_PITCH 75

// disable pjsua logging for a value of 0 (possible logging levels: 1-6)
#define PJSUA_LOG_LEVEL 0 


// struct for app configuration settings
struct app_config {
	char *sip_domain;
	char *sip_user;
	char *sip_password;
	char *phone_number;
	char *tts;
	char *tts_file;
	int record_call;
	char *record_file;
	int repetition_limit;
	int silent_mode;
} app_cfg;

// first set some default values
app_cfg.tts_file = "play.wav";
app_cfg.record_call = 0;
app_cfg.repetition_limit = 3;
app_cfg.silent_mode = 0;

// global helper vars
int call_confirmed = 0;
int media_counter = 0;
int app_exiting = 0;

// global vars for pjsua
pjsua_acc_id acc_id;
pjsua_player_id play_id = PJSUA_INVALID_ID;
pjmedia_port *play_port;
pjsua_recorder_id rec_id = PJSUA_INVALID_ID;

// header of helper-methods
static void create_player(pjsua_call_id);
static void create_recorder(pjsua_call_info);
static void log_message(char *);
static void make_sip_call();
static void register_sip(void);
static void setup_sip(void);
static void tts_google(char *);
static void tts_espeak(char *);
static void usage(int);
static int try_get_argument(int, char *, char **, int, char *[]);

// header of callback-methods
static void on_call_media_state(pjsua_call_id);
static void on_call_state(pjsua_call_id, pjsip_event *);
static pj_status_t on_media_finished(pjmedia_port *, void *);
static void signal_handler(int);

// header of app-control-methods
static void app_exit();
static void error_exit(const char *, pj_status_t);

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
		logprintf(LOG_DEBUG, "using TTS\n");
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jtts, "value");

		if(jvalues1 != NULL && jvalues2 != NULL) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {

				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -tts %s -ttsf %s/play.wav", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, jval2->string_, ssipttspath);
				logprintf(LOG_DEBUG, sipcmd);
				app_cfg.sip_domain = ssipdomain;
				app_cfg.sip_user = ssipuser;
				app_cfg.sip_password = ssippassword;
				app_cfg.phone_number = jval1->string_;
				app_cfg.tts = jval2->string_;
				app_cfg.tts_file = ssipttspath + "/play.wav";
				if(do_sipcall() != 0) {
					logprintf(LOG_ERR, "Sipcall failed to call \"%s\"", jval1->string_);
				}
			}
		}
	}
	// TTSFILE is given, no tts
	if(jphonenumber != NULL && jttsfile != NULL) {
		logprintf(LOG_DEBUG, "using TTSFILE\n");
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jttsfile, "value");

		if(jvalues1 != NULL && jvalues2 != NULL) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {

				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -ttsf %s/%s", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, ssipttspath, jval2->string_);
				logprintf(LOG_DEBUG, sipcmd);
				app_cfg.sip_domain = ssipdomain;
				app_cfg.sip_user = ssipuser;
				app_cfg.sip_password = ssippassword;
				app_cfg.phone_number = jval1->string_;
				app_cfg.tts = null;
				app_cfg.tts_file = ssipttspath + "/" + jval2->string_;
				if(do_sipcall() != 0) {
					logprintf(LOG_ERR, "Sipcall failed to call \"%s\"", jval1->string_);
				}
			}
		}
	}

	// TTS is given, use play.wav as ttsfile
	if(jphonenumber != NULL && jtts != NULL) {
		logprintf(LOG_DEBUG, "using TTS\n");
		jvalues1 = json_find_member(jphonenumber, "value");
		jvalues2 = json_find_member(jttsfile, "value");

		if(jvalues1 != NULL && jvalues2 != NULL) {
			jval1 = json_find_element(jvalues1, 0);
			jval2 = json_find_element(jvalues2, 0);
			if(jval1 != NULL && jval2 != NULL &&
				jval1->tag == JSON_STRING && jval2->tag == JSON_STRING) {

				sprintf(sipcmd, "%s -sd %s -su %s -sp %s -pn %s -tts %s -ttsf %s/play.wav", ssipprogram, ssipdomain, ssipuser, ssippassword, jval1->string_, jval2->string_, ssipttspath);
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
	options_add(&action_sipcall->options, 'b', "TTS", OPTION_OPT_VALUE, DEVICES_VALUE, JSON_STRING, NULL, NULL);
	options_add(&action_sipcall->options, 'c', "TTSFILE", OPTION_OPT_VALUE, DEVICES_VALUE, JSON_STRING, NULL, NULL);

	action_sipcall->run = &run;
	action_sipcall->checkArguments = &checkArguments;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
	module->name = "sipcall";
	module->version = "1.0";
	module->reqversion = "6.0";
	module->reqcommit = NULL;
}

void init(void) {
	actionSipcallInit();
}
#endif

// ==== sipcall =================================================================================================== //


// main application
static int sipcall() {
	// print infos
	log_message("SIP Call - Simple TTS-based Automated Calls\n");
	log_message("===========================================\n");

	// register signal handler for break-in-keys (e.g. ctrl+c)
	signal(SIGINT, signal_handler);
	signal(SIGKILL, signal_handler);

	// synthesize speech 
	// iff tts param set, synthesize speech to file
	if(app_cfg.tts)
	{
		// use google of espeak for syntesizing speech
		// disabled: tts_espeak(app_cfg.tts_file);
		tts_google(app_cfg.tts_file);
	}
	// setup up sip library pjsua
	setup_sip();

	// create account and register to sip server
	register_sip();

	// initiate call
	make_sip_call();

	// app loop
	for(;;) {}

	// exit app
	app_exit();

	return 0;
}


// helper for logging messages to console (disabled if silent mode is active)
static void log_message(char *message)
{
	if(!app_cfg.silent_mode)
	{
		fprintf(stderr, message);
	}
}

// helper for setting up sip library pjsua
static void setup_sip(void)
{
	pj_status_t status;

	log_message("Setting up pjsua ... \n");

	// create pjsua  
	status = pjsua_create();
	if(status != PJ_SUCCESS) error_exit("Error in pjsua_create()", status);

	// configure pjsua	
	pjsua_config cfg;
	pjsua_config_default(&cfg);

	// enable just 1 simultaneous call 
	cfg.max_calls = 1;

	// callback configuration		
	cfg.cb.on_call_media_state = &on_call_media_state;
	cfg.cb.on_call_state = &on_call_state;

	// logging configuration
	pjsua_logging_config log_cfg;
	pjsua_logging_config_default(&log_cfg);
	log_cfg.console_level = PJSUA_LOG_LEVEL;

	// media configuration
	pjsua_media_config media_cfg;
	pjsua_media_config_default(&media_cfg);
	media_cfg.snd_play_latency = 160;  //default 2000
	media_cfg.snd_rec_latency = 160;  //default 2000
	media_cfg.clock_rate = 8000;
	media_cfg.snd_clock_rate = 8000;
	media_cfg.quality = 5;
	media_cfg.ec_tail_len = 0;

	// initialize pjsua 
	status = pjsua_init(&cfg, &log_cfg, &media_cfg);
	if(status != PJ_SUCCESS) error_exit("Error in pjsua_init()", status);

	// add udp transport
	pjsua_transport_config udpcfg;
	pjsua_transport_config_default(&udpcfg);

	udpcfg.port = 5060;
	status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &udpcfg, NULL);
	if(status != PJ_SUCCESS) error_exit("Error creating transport", status);

	// initialization is done, start pjsua
	status = pjsua_start();
	if(status != PJ_SUCCESS) error_exit("Error starting pjsua", status);

	// disable sound - use null sound device
	status = pjsua_set_null_snd_dev();
	if(status != PJ_SUCCESS) error_exit("Error disabling audio", status);

	log_message("Done.\n");
}

// helper for creating and registering sip-account
static void register_sip(void)
{
	pj_status_t status;

	log_message("Registering account ... \n");

	// prepare account configuration
	pjsua_acc_config cfg;
	pjsua_acc_config_default(&cfg);

	// build sip-user-url
	char sip_user_url[40];
	sprintf(sip_user_url, "sip:%s@%s", app_cfg.sip_user, app_cfg.sip_domain);

	// build sip-provder-url
	char sip_provider_url[40];
	sprintf(sip_provider_url, "sip:%s", app_cfg.sip_domain);

	// create and define account
	cfg.id = pj_str(sip_user_url);
	cfg.reg_uri = pj_str(sip_provider_url);
	cfg.cred_count = 1;
	cfg.cred_info[0].realm = pj_str(app_cfg.sip_domain);
	cfg.cred_info[0].scheme = pj_str("digest");
	cfg.cred_info[0].username = pj_str(app_cfg.sip_user);
	cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	cfg.cred_info[0].data = pj_str(app_cfg.sip_password);

	// add account
	status = pjsua_acc_add(&cfg, PJ_TRUE, &acc_id);
	if(status != PJ_SUCCESS) error_exit("Error adding account", status);

	log_message("Done.\n");
}

// helper for making calls over sip-account
static void make_sip_call()
{
	pj_status_t status;

	log_message("Starting call ... \n");

	// build target sip-url
	char sip_target_url[40];
	sprintf(sip_target_url, "sip:%s@%s", app_cfg.phone_number, app_cfg.sip_domain);

	// start call with sip-url
	pj_str_t uri = pj_str(sip_target_url);
	status = pjsua_call_make_call(acc_id, &uri, 0, NULL, NULL, NULL);
	if(status != PJ_SUCCESS) error_exit("Error making call", status);

	log_message("Done.\n");
}

// helper for creating call-media-player
static void create_player(pjsua_call_id call_id)
{
	// get call infos
	pjsua_call_info ci;
	pjsua_call_get_info(call_id, &ci);

	pj_str_t name;
	pj_status_t status = PJ_ENOTFOUND;

	log_message("Creating player ... \n");

	// create player for playback media		
	status = pjsua_player_create(pj_cstr(&name, app_cfg.tts_file), 0, &play_id);
	if(status != PJ_SUCCESS) error_exit("Error playing sound-playback", status);

	// connect active call to media player
	pjsua_conf_connect(pjsua_player_get_conf_port(play_id), ci.conf_slot);

	// get media port (play_port) from play_id
	status = pjsua_player_get_port(play_id, &play_port);
	if(status != PJ_SUCCESS) error_exit("Error getting sound player port", status);

	// register media finished callback	
	status = pjmedia_wav_player_set_eof_cb(play_port, NULL, &on_media_finished);
	if(status != PJ_SUCCESS) error_exit("Error adding sound-playback callback", status);

	log_message("Done.\n");
}

// helper for creating call-recorder
static void create_recorder(pjsua_call_info ci)
{
	// specify target file
	pj_str_t rec_file = pj_str(app_cfg.record_file);
	pj_status_t status = PJ_ENOTFOUND;

	log_message("Creating recorder ... \n");

	// Create recorder for call
	status = pjsua_recorder_create(&rec_file, 0, NULL, 0, 0, &rec_id);
	if(status != PJ_SUCCESS) error_exit("Error recording answer", status);

	// connect active call to call recorder
	pjsua_conf_port_id rec_port = pjsua_recorder_get_conf_port(rec_id);
	pjsua_conf_connect(ci.conf_slot, rec_port);

	log_message("Done.\n");
}

// Google TTS:
static void tts_google(char *file)
{
	log_message("Synthesizing speech with Google Translate ... \n");
	int speech_status = -1;
	char wget_command[200];
	sprintf(wget_command, "wget -q -U Mozilla -O %s.mp3 \"http://translate.google.com/translate_tts?tl=nl&q=%s\"", file, app_cfg.tts);
	log_message(wget_command);
	log_message("\n");
	speech_status = system(wget_command);
	if(speech_status != 0) error_exit("Error in Google TTS API", speech_status);
	char sox_command[200];
	sprintf(sox_command, "sox %s.mp3 -r 22050 -b 16 -c 1 %s", file, file);
	log_message(sox_command);
	log_message("\n");
	speech_status = system(sox_command);
	if(speech_status != 0) error_exit("Error while converting mp3 to wav", speech_status);
	log_message("Done.\n");
}

// synthesize speech / create message via espeak
static void tts_espeak(char *file)
{
	log_message("Synthesizing speech with eSpeak ... \n");

	int speech_status = -1;
	char speech_command[200];
	sprintf(speech_command, "espeak -v%s -a%i -k%i -s%i -p%i -w %s '%s'", ESPEAK_LANGUAGE, ESPEAK_AMPLITUDE, ESPEAK_CAPITALS_PITCH, ESPEAK_SPEED, ESPEAK_PITCH, file, app_cfg.tts);
	log_message(speech_command);
	log_message("\n");
	speech_status = system(speech_command);
	if(speech_status != 0) error_exit("Error while creating phone text", speech_status);

	log_message("Done.\n");
}

// handler for call-media-state-change-events
static void on_call_media_state(pjsua_call_id call_id)
{
	// get call infos
	pjsua_call_info ci;
	pjsua_call_get_info(call_id, &ci);

	pj_status_t status = PJ_ENOTFOUND;

	// check state if call is established/active
	if(ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {

		log_message("Call media activated.\n");

		// create and start media player
		create_player(call_id);

		// create and start call recorder
		if(app_cfg.record_call)
		{
			create_recorder(ci);
		}
	}
}

// handler for call-state-change-events
static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
	// get call infos
	pjsua_call_info ci;
	pjsua_call_get_info(call_id, &ci);

	// prevent warning about unused argument e
	PJ_UNUSED_ARG(e);

	// check call state
	if(ci.state == PJSIP_INV_STATE_CONFIRMED)
	{
		log_message("Call confirmed.\n");

		call_confirmed = 1;

		// ensure that message is played from start
		if(play_id != PJSUA_INVALID_ID)
		{
			pjmedia_wav_player_port_set_pos(play_port, 0);
		}
	}
	if(ci.state == PJSIP_INV_STATE_DISCONNECTED)
	{
		log_message("Call disconnected.\n");

		// exit app if call is finished/disconnected
		app_exit();
	}
}

// handler for media-finished-events
static pj_status_t on_media_finished(pjmedia_port *media_port, void *user_data)
{
	PJ_UNUSED_ARG(media_port);
	PJ_UNUSED_ARG(user_data);

	if(call_confirmed)
	{
		// count repetition
		media_counter++;

		// exit app if repetition limit is reached
		if(app_cfg.repetition_limit <= media_counter)
		{
			app_exit();
		}
	}

	pj_status_t status;
	return status;
}

// handler for "break-in-key"-events (e.g. ctrl+c)
static void signal_handler(int signal)
{
	// exit app
	app_exit();
}

// clean application exit
static void app_exit()
{
	if(!app_exiting)
	{
		app_exiting = 1;
		log_message("Stopping application ... \n");

		// check if player/recorder is active and stop them
		if(play_id != -1) pjsua_player_destroy(play_id);
		if(rec_id != -1) pjsua_recorder_destroy(rec_id);

		// hangup open calls and stop pjsua
		pjsua_call_hangup_all();
		pjsua_destroy();

		log_message("Done.\n");

		exit(0);
	}
}

// display error and exit application
static void error_exit(const char *title, pj_status_t status)
{
	if(!app_exiting)
	{
		app_exiting = 1;

		pjsua_perror("SIP Call", title, status);

		// check if player/recorder is active and stop them
		if(play_id != -1) pjsua_player_destroy(play_id);
		if(rec_id != -1) pjsua_recorder_destroy(rec_id);

		// hangup open calls and stop pjsua
		pjsua_call_hangup_all();
		pjsua_destroy();

		exit(1);
	}
}
