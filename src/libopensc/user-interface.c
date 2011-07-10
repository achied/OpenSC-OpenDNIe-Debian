/**
 * user-interface.c: Support for GUI functions
 *
 * This file contains code for several related user-interface
 * functions:
 * - Ask user confirmation
 * - Let user enter pin
 *
 * Copyright (C) 2010 Juan Antonio Martinez <jonsito@terra.es>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define __USER_INTERFACE_C__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef _WIN32
#define UNICODE
#include <windows.h>
#endif
#ifdef __APPLE__
#include <Carbon/Carbon.h>
#endif
#ifdef linux
/* default user consent program (if required) */
#define USER_CONSENT_CMD "/usr/bin/pinentry"
#endif 
/* default titles */
#define USER_CONSENT_TITLE "Confirm"
#define USER_CONSENT_PROMPT "An User Consent has been requested:"
#define USER_PIN_TITLE "PIN Request"
#define USER_PIN_PROMPT "Enter PIN:"

#include "libopensc/opensc.h"
#include "libopensc/log.h"
#include "libopensc/user-interface.h"

/**
 * Messages used on pinentry protocol
 */
char *user_consent_msgs[] = { "SETTITLE", "SETDESC", "CONFIRM", "BYE" };
char *user_pin_msgs[] = { "SETTITLE", "SETPROMPT", "GETPIN", "BYE" };

/********** architecture dependent routines *****************************/

#ifdef __APPLE__

/* In Mac OSX use native "xxxDisplayAlert" functions */
static int ui_ask_user_consent(
        sc_context_t *ctx,      /* Card context */
	char *user_consent_app, /* (ignored) user consent application */
        const char *title,      /* Title of the window */
        const char *msg         /* Text to be shown to the user */
	) {

	CFOptionFlags result;  // result code from the message box
	CFStringRef header_ref; // to store title
	CFStringRef message_ref; // to store message

	// convert the strings from char* to CFStringRef
	header_ref = CFStringCreateWithCString(NULL,title,strlen(title));
	message_ref = CFStringCreateWithCString(NULL,message,strlen(message));
	
	/* stupid trick to avoid "unused" warning */
	sc_log(ctx,"Notice: Windows ignores '%s' User Consent app",user_consent_app);

	// Displlay user notification alert
	CFUserNotificationDisplayAlert(
		0, // no timeout
		kCFUserNotificationNoteAlertLevel,  // Alert level
		NULL,	// IconURL, use default, you can change
			// it depending message_type flags
		NULL,	// SoundURL (not used)
		NULL,	//localization of strings
		header_ref,	// header. Cannot be null
		message_ref,	//message text
		CFSTR("Cancel"), // default ( "OK" if null) button text
		CFSTR("OK"), // second button title
                NULL, // third button title, null--> no other button
		&result //response flags
	);

	//Clean up the strings
	CFRelease( header_ref );
        CFRelease( message_ref );
	// Return 0 only if "OK" is selected
	if( result == kCFUserNotificationAlternateResponse ) return SC_SUCCESS;
	return SC_ERROR_NOT_ALLOWED;
}

#endif

#ifdef _WIN32

/* in Windows, do not use pinentry, but MessageBox system call */

static int ui_ask_user_consent(
        sc_context_t *ctx,      /* Card context */
	char *user_consent_app, /* ignored */
        const char *title,      /* Title of the window */
        const char *message     /* Text to be shown to the user */
	) {

	/* stupid trick to avoid "unused" warning */
	sc_log(ctx,"Notice: Windows ignores '%s' User Consent app",user_consent_app);
	int res = MessageBox (
		NULL,
		TEXT(message),
		TEXT(title),
		MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2 | MB_APPLMODAL
		);
	return ( res == IDOK )? SC_SUCCESS : SC_ERROR_NOT_ALLOWED;
}

#endif

#ifdef linux

/* Use pinentry by mean of a bidirectional pipe to avoid external deps */

static int ui_ask_user_consent(
        sc_context_t *ctx,      /* Card context */
	char *user_consent_app, /* pinentry pathname */
        const char *title,      /* Title of the window */
        const char *message     /* Text to be shown to the user */
	) {

	pid_t pid;
	char *msg;		/* to store error messages */
	FILE *fin, *fout;	/* to handle pipes as streams */
	struct stat st_file;	/* to verify that executable exists */
	int srv_send[2];	/* to send data from server to client */
	int srv_recv[2];	/* to receive data from client to server */
	char outbuf[1024];	/* to compose and send messages */	
	char buf[1024];		/* to store client responses */
	int n = 0;		/* to iterate on to-be-sent messages */
	int res = SC_SUCCESS;   /* returned result */

	/* check that user_consent_app exists. TODO: check if executable */
	res = stat(user_consent_app, &st_file);
	if (res != 0) {
		sc_log(ctx,"Invalid pinentry application: %s",user_consent_app);
		LOG_FUNC_RETURN(ctx, SC_ERROR_INVALID_ARGUMENTS);
	}

	/* just a simple bidirectional pipe+fork+exec implementation */
	/* In a pipe, xx[0] is for reading, xx[1] is for writing */
	if (pipe(srv_send) < 0) {
		msg = "pipe(srv_send)";
		goto do_error;
	}
	if (pipe(srv_recv) < 0) {
		msg = "pipe(srv_recv)";
		goto do_error;
	}
	pid = fork();
	switch (pid) {
	case -1: /* error  */
		msg = "fork()";
		goto do_error;
	case 0:	/* child  */
		/* make our pipes, our new stdin & stderr, closing older ones */
		dup2(srv_send[0], STDIN_FILENO);	/* map srv send for input */
		dup2(srv_recv[1], STDOUT_FILENO);	/* map srv_recv for output */
		/* once dup2'd pipes are no longer needed on client; so close */
		close(srv_send[0]);
		close(srv_send[1]);
		close(srv_recv[0]);
		close(srv_recv[1]);
		/* call exec() with proper user_consent_app from configuration */
		execlp(user_consent_app, user_consent_app, (char *)NULL);	/* if ok should never return */
		res = SC_ERROR_INTERNAL;
		msg = "execlp() error";	/* exec() failed */
		goto do_error;
	default: /* parent */
		/* Close the pipe ends that the child uses to 
		 * read from / write to so when we close the others, 
		 * an EOF will be transmitted properly.
		 */
		close(srv_send[0]);
		close(srv_recv[1]);
		/* use iostreams to take care on newlines and text based data */
		fin = fdopen(srv_recv[0], "r");
		if (fin == NULL) {
			msg = "fdopen(in)";
			goto do_error;
		}
		fout = fdopen(srv_send[1], "w");
		if (fout == NULL) {
			msg = "fdopen(out)";
			goto do_error;
		}
		/* read and ignore first line */
		fflush(stdin);
		for (n = 0; n<4; n++) {
			char *pt;
			memset(outbuf, 0, sizeof(outbuf));
			if (n==0) snprintf(outbuf,1023,"%s %s\n",user_consent_msgs[0],title);
			else if (n==1) snprintf(outbuf,1023,"%s %s\n",user_consent_msgs[1],message);
			else snprintf(outbuf,1023,"%s\n",user_consent_msgs[n]);
			/* send message */
			fputs(outbuf, fout);
			fflush(fout);
			/* get response */
			memset(buf, 0, sizeof(buf));
			pt=fgets(buf, sizeof(buf) - 1, fin);
			if (pt==NULL) {
				res = SC_ERROR_INTERNAL;
				msg = "fgets() Unexpected IOError/EOF";
				goto do_error;
			}
			if (strstr(buf, "OK") == NULL) {
				res = SC_ERROR_NOT_ALLOWED;
				msg = "fail/cancel";
				goto do_error;
			}
		}
	}			/* switch */
	/* arriving here means signature has been accepted by user */
	res = SC_SUCCESS;
	msg = NULL;
do_error:
	/* close out channel to force client receive EOF and also die */
	if (fout != NULL) fclose(fout);
	if (fin != NULL) fclose(fin);
	if (msg != NULL) sc_log(ctx, "%s", msg);
	return res;
}

#endif


static int ui_ask_user_pin(
	sc_context_t *ctx,	/* Card context */
	const char *title,	/* Title of the window */
	const char *msg,	/* Text to be shown to the user */
	char *pinbuf,		/* Where to store the user entered data */
	int *pinlen) {		/* buffer length; on return user data length */

	/* TODO: write :-) */
	return SC_ERROR_NOT_SUPPORTED;
}

/********** common public routines ***************************************/

/**
 * Ask user for pin.
 * 
 * Check the user pin configuration,
 * Invoke proper gui app and check result
 * 
 * @param card pointer to sc_card structure
 * @param title Text to appear in the window header
 * @param pin Structure to handle/store pin related data
 * @return SC_SUCCESS if user accepts , else error code
 */
int sc_ask_user_pin(sc_card_t * card, const char *title, struct sc_pin_cmd_pin *pin) {
	char *pinbuf=NULL;
	int pinlen=0;
	int res=SC_ERROR_INTERNAL;
	char *msg=NULL;
	
	if ( (card==NULL) || (card->ctx==NULL) ) 
		return SC_ERROR_INVALID_ARGUMENTS;
	LOG_FUNC_CALLED(card->ctx);
	if (pin==NULL) LOG_FUNC_RETURN(card->ctx,SC_ERROR_INVALID_ARGUMENTS);

	/* use a temporary buffer to ask for pin */
	if (pin->max_length<=0) {
		msg="Invalid pin max lenght";
		res=SC_ERROR_INVALID_ARGUMENTS;
		goto ask_user_pin_end;
	}
	pinlen=pin->max_length;

	pinbuf= calloc(pin->max_length, sizeof(char));
	if (!pinbuf) {
		msg="Cannot create pin buffer";
		res=SC_ERROR_OUT_OF_MEMORY;
		goto ask_user_pin_end;
	}

	res= ui_ask_user_pin(
		card->ctx,
		(title==NULL)?USER_PIN_TITLE:title,
		(pin->prompt==NULL)?USER_PIN_PROMPT:pin->prompt,
		pinbuf,
		&pinlen);
	if (res!=SC_SUCCESS) {
		msg="Error in ui_ask_user_pin";
		goto ask_user_pin_end;
	}

	/* TODO: parse received data and fill result structure */

	/* arriving here means success */
	res=SC_SUCCESS;

ask_user_pin_end:
	if (msg!=NULL) sc_log(card->ctx,msg);
	if (pinbuf!=NULL) {
		memset(pinbuf,0,pinlen);
		free(pinbuf);
	}
	LOG_FUNC_RETURN(card->ctx,res);
}


/**
 * Ask for user consent.
 *
 * Check for user consent configuration,
 * Invoke proper gui app and check result
 *
 * @param card pointer to sc_card structure
 * @param title Text to appear in the window header
 * @param text Message to show to the user
 * @return SC_SUCCESS on user consent OK , else error code
 */
int sc_ask_user_consent(sc_card_t * card, const char *title, const char *message)
{
	sc_card_ui_context_t *ui_context;
	int res = SC_ERROR_INTERNAL;	/* by default error :-( */
	char *msg = NULL;	/* to makr errors */

	if ((card == NULL) || (card->ctx == NULL))
		return SC_ERROR_INVALID_ARGUMENTS;
	LOG_FUNC_CALLED(card->ctx);

	if ((title==NULL) || (message==NULL)) 
		LOG_FUNC_RETURN(card->ctx, SC_ERROR_INVALID_ARGUMENTS);

	ui_context=card->ui_context;
	if (ui_context==NULL) {
		sc_log(card->ctx,
		       "Warning: User Consent Called, but no configuration data");
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	} 

	if (ui_context->user_consent_enabled == 0) {
		sc_log(card->ctx,
		       "User Consent is disabled in configuration file");
		LOG_FUNC_RETURN(card->ctx, SC_SUCCESS);
	}
	
	/* call to arch-dependent user consent routine */
	res= ui_ask_user_consent(
		card->ctx,
		ui_context->user_consent_app,
		(title==NULL)?USER_CONSENT_TITLE:title,
		(message==NULL)?USER_CONSENT_PROMPT: message );
	if (res != SC_SUCCESS) sc_log(card->ctx, "Error in Ask User Consent");
	LOG_FUNC_RETURN(card->ctx, res);
}

