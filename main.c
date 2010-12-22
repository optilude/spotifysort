/**
 * Copyright (c) 2006-2010 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * This example application shows parts of the playlist and player submodules.
 * It also shows another way of doing synchronization between callbacks and
 * the main thread.
 *
 * This file is part of the libspotify examples suite.
 */

#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <libspotify/api.h>

#include "playlist.h"

/* --- Data --- */
/// The application key is specific to each project, and allows Spotify
/// to produce statistics on how our service is used.
extern const uint8_t g_appkey[];
/// The size of the application key.
extern const size_t g_appkey_size;

/// Synchronization mutex for the main thread
static pthread_mutex_t g_notify_mutex;
/// Synchronization condition variable for the main thread
static pthread_cond_t g_notify_cond;
/// Synchronization variable telling the main thread to process events
static int g_notify_do;

/// Synchronization variable telling the main thread to quit
static int g_quit;

/* ---------------------------  SESSION CALLBACKS  ------------------------- */
/**
 * This callback is called when an attempt to login has succeeded or failed.
 *
 * @sa sp_session_callbacks#logged_in
 */
static void logged_in(sp_session *sess, sp_error error)
{
	
	sp_user *me;
	const char *my_name;
	
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Failed to log in to Spotify: %s\n",
				sp_error_message(error));
		sp_session_release(sess);
		exit(4);
	}
	
	me = sp_session_user(sess);
	my_name = (sp_user_is_loaded(me) ? sp_user_display_name(me) : sp_user_canonical_name(me));
	fprintf(stderr, "Logged in to Spotify as user %s\n", my_name);
	
	sort_playlists(sess);
	
	sp_session_logout(sess);
	
	g_quit = 1;
	
}

/**
 * This callback is called from an internal libspotify thread to ask us to
 * reiterate the main loop.
 *
 * We notify the main thread using a condition variable and a protected variable.
 *
 * @sa sp_session_callbacks#notify_main_thread
 */
static void notify_main_thread(sp_session *sess)
{
	pthread_mutex_lock(&g_notify_mutex);
	g_notify_do = 1;
	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
}

/**
 * This callback is called for log messages.
 *
 * @sa sp_session_callbacks#log_message
 */
static void log_message(sp_session *session, const char *data)
{
	fprintf(stderr, "%s", data);
}

/**
 * The session callbacks
 */
static sp_session_callbacks session_callbacks = {
	.logged_in = &logged_in,
	.notify_main_thread = &notify_main_thread,
	.music_delivery = NULL,
	.metadata_updated = NULL,
	.play_token_lost = NULL,
	.log_message = log_message,
	.end_of_track = NULL,
};

/**
 * The session configuration. Note that application_key_size is an external, so
 * we set it in main() instead.
 */
static sp_session_config spconfig = {
	.api_version = SPOTIFY_API_VERSION,
	.cache_location = "/tmp/spotifysort",
	.settings_location = "/tmp/spotifysort",
	.application_key = g_appkey,
	.application_key_size = 0, // Set in main()
	.user_agent = "SpotifySorter",
	.callbacks = &session_callbacks,
	NULL,
};
/* -------------------------  END SESSION CALLBACKS  ----------------------- */


/**
 * Show usage information
 *
 * @param  progname  The program name
 */
static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s -u <username> -p <password>\n", progname);
}

static void trim(char *buf)
{
	size_t l = strlen(buf);
	while(l > 0 && buf[l - 1] < 32)
		buf[--l] = 0;
}

int main(int argc, char **argv)
{
	sp_session *sp;
	sp_error err;
	int next_timeout = 0;
	const char *username = NULL;
	const char *password = NULL;
	char username_buf[256];
	int opt;
	
	while ((opt = getopt(argc, argv, "u:p:")) != EOF) {
		switch (opt) {
			case 'u':
				username = optarg;
				break;
				
			case 'p':
				password = optarg;
				break;
				
			default:
				usage(basename(argv[0]));
				exit(1);
		}
	}
	
	if (username == NULL) {
		printf("Username: ");
		fflush(stdout);
		fgets(username_buf, sizeof(username_buf), stdin);
		trim(username_buf);
		username = username_buf;
	}
	
	if (password == NULL)
		password = getpass("Password: ");
	
	if (!username || !password) {
		usage(basename(argv[0]));
		exit(1);
	}
	
	/* Create session */
	spconfig.application_key_size = g_appkey_size;
	
	err = sp_session_create(&spconfig, &sp);
	
	if (SP_ERROR_OK != err) {
		fprintf(stderr, "Unable to create session: %s\n",
				sp_error_message(err));
		exit(1);
	}
	
	pthread_mutex_init(&g_notify_mutex, NULL);
	pthread_cond_init(&g_notify_cond, NULL);
	
	sp_session_login(sp, username, password);
	pthread_mutex_lock(&g_notify_mutex);
	
	g_quit = 0;
	
	while(!g_quit) {
		
		if (next_timeout == 0) {
			while(!g_notify_do)
				pthread_cond_wait(&g_notify_cond, &g_notify_mutex);
		} else {
			struct timespec ts;
			
#if _POSIX_TIMERS > 0
			clock_gettime(CLOCK_REALTIME, &ts);
#else
			struct timeval tv;
			gettimeofday(&tv, NULL);
			TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif
			ts.tv_sec += next_timeout / 1000;
			ts.tv_nsec += (next_timeout % 1000) * 1000000;
			
			pthread_cond_timedwait(&g_notify_cond, &g_notify_mutex, &ts);
		}
		
		g_notify_do = 0;
		pthread_mutex_unlock(&g_notify_mutex);
		
		do {
			sp_session_process_events(sp, &next_timeout);
		} while (next_timeout == 0);
		
		pthread_mutex_lock(&g_notify_mutex);
	}
	
	return 0;
}
