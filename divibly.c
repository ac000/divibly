/*
 * divibly - A simple DVB-T viewer
 *
 * Copyright (C) 2014		Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#define	_GNU_SOURCE	/* SA_RESTART, timer_*() */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include <xosd.h>

#include <vlc/vlc.h>

#define MAX_CHANNELS	 255
#define BANDWIDTH_MHZ	   8

#define OSD_FONT	"-adobe-utopia-bold-r-normal--96-0-0-0-p-0-iso8859-1"
#define OSD_TIMEOUT	2

#define BUF_SIZE	4096

static libvlc_media_player_t *media_player;
static libvlc_instance_t *vlc_inst;
static bool fullscreen = true;
static int nr_channels;
static int chan_idx;
static xosd *osd_display;
static timer_t osd_timerid;

struct chan_info {
	char *name;
	unsigned int freq;
	unsigned int bandwidth;
	unsigned int pid;
};

static struct chan_info channels[MAX_CHANNELS];

static void destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static void kill_osd(int sig, siginfo_t *si, void *uc)
{
	timer_delete(osd_timerid);

	if (!osd_display)
		return;

	xosd_destroy(osd_display);
	osd_display = NULL;
}

static void set_spu(void)
{
	static int spu = -1;
	libvlc_track_description_t *desc;

	/* Toggle subtitles off */
	if (spu > -1) {
		spu = -1;
		goto out;
	}

	/* Find the subtitle id */
	desc = libvlc_video_get_spu_description(media_player);
	while (desc) {
		spu = desc->i_id;
		if (spu > -1)
			break;
		desc = desc->p_next;
	}

out:
	libvlc_video_set_spu(media_player, spu);
}

static void set_osd_timer(void)
{
	struct itimerspec its;
	struct sigevent sev;
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_sigaction = kill_osd;
	sigaction(SIGRTMIN, &action, NULL);

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &osd_timerid;
	timer_create(CLOCK_MONOTONIC, &sev, &osd_timerid);

	its.it_value.tv_sec = OSD_TIMEOUT;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = its.it_value.tv_sec;
	its.it_interval.tv_nsec = its.it_value.tv_nsec;

	timer_settime(osd_timerid, 0, &its, NULL);
}

static void get_channel_info(const char *channels_conf)
{
	char buf[BUF_SIZE];
	int i = 0;
	FILE *fp;

	fp = fopen(channels_conf, "r");
	while (fgets(buf, BUF_SIZE, fp)) {
		sscanf(buf, "%m[^:]:%u:%*m[^:]:%*m[^:]:%*m[^:]:%*m[^:]:"
				"%*m[^:]:%*m[^:]:%*m[^:]:%*m[^:]:%*u:%*u:%u",
				&channels[i].name, &channels[i].freq,
				&channels[i].pid);
		channels[i].bandwidth = BANDWIDTH_MHZ;
		i++;
		if (i == MAX_CHANNELS) {
			fprintf(stderr, "Max number of channels reached. "
				"Please increase MAX_CHANNELS\n");
			exit(EXIT_FAILURE);
		}
	}
	nr_channels = i;

	fclose(fp);
}

static void play_channel(void)
{
	libvlc_media_t *media;
	char f_opt[64];
	char b_opt[64];
	char p_opt[64];

	if (chan_idx >= nr_channels)
		chan_idx = 0;
	else if (chan_idx < 0)
		chan_idx = nr_channels - 1;

	snprintf(f_opt, sizeof(f_opt), ":dvb-frequency=%u",
			channels[chan_idx].freq);
	snprintf(b_opt, sizeof(b_opt), ":dvb-bandwidth=%u",
			channels[chan_idx].bandwidth);
	snprintf(p_opt, sizeof(p_opt), ":program=%u",
			channels[chan_idx].pid);

	media = libvlc_media_new_location(vlc_inst, "dvb://");
	libvlc_media_add_option(media, f_opt);
	libvlc_media_add_option(media, b_opt);
	libvlc_media_add_option(media, p_opt);
	libvlc_media_player_set_media(media_player, media);
	libvlc_media_release(media);

	libvlc_media_player_play(media_player);
}

static void cb_input(GtkWidget *window, GdkEventKey *event, gpointer data)
{
	switch (event->keyval) {
	case GDK_KEY_f:
	case GDK_KEY_F:
		if (!fullscreen) {
			gtk_window_fullscreen(GTK_WINDOW(window));
			fullscreen = true;
		} else {
			gtk_window_unfullscreen(GTK_WINDOW(window));
			fullscreen = false;
		}
		break;
	case GDK_KEY_0:
		chan_idx = 9;
		play_channel();
		break;
	case GDK_KEY_1 ... GDK_KEY_9:
		chan_idx = event->keyval - GDK_KEY_1;
		play_channel();
		break;
	case GDK_KEY_Up:
		chan_idx++;
		play_channel();
		break;
	case GDK_KEY_Down:
		chan_idx--;
		play_channel();
		break;
	case GDK_KEY_s:
	case GDK_KEY_S:
		set_spu();
		break;
	case GDK_KEY_q:
	case GDK_KEY_Q:
	case GDK_KEY_Escape:
		gtk_main_quit();
	}
}

static void cb_realize(GtkWidget *widget, gpointer data)
{
	libvlc_media_player_set_xwindow(media_player, GDK_WINDOW_XID(
				gtk_widget_get_window(widget)));

	libvlc_video_set_deinterlace(media_player, "yadiff2x");

	play_channel();
}

static void cb_set_title(const struct libvlc_event_t *ev, void *data)
{
	GtkWindow *window = GTK_WINDOW(data);
	char title[255];

	snprintf(title, sizeof(title), "divibly (%s)",
			channels[chan_idx].name);
	gtk_window_set_title(window, title);

	if (osd_timerid) {
		timer_delete(osd_timerid);
		xosd_destroy(osd_display);
	}

	osd_display = xosd_create(1);
	xosd_set_font(osd_display, OSD_FONT);
	xosd_set_colour(osd_display, "white");
	xosd_display(osd_display, 0, XOSD_string, channels[chan_idx].name);

	set_osd_timer();
}

int main(int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *player;
	const GdkColor player_bg_color = { 0, 0, 0, 0 };
	libvlc_event_manager_t *vevent;

	if (argc < 2) {
		fprintf(stderr, "Usage: divibly /path/to/channels.conf\n");
		exit(EXIT_FAILURE);
	}

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);
	g_signal_connect(window, "key_press_event",G_CALLBACK(cb_input), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 0);
	gtk_window_set_default_size(GTK_WINDOW(window), 1024, 576);
	gtk_window_fullscreen(GTK_WINDOW(window));
	gtk_window_set_title(GTK_WINDOW(window), "divibly");

	get_channel_info(argv[1]);

	player = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(window), player);
	gtk_widget_modify_bg(player, GTK_STATE_NORMAL, &player_bg_color);

	vlc_inst = libvlc_new(0, NULL);
	media_player = libvlc_media_player_new(vlc_inst);
	g_signal_connect(G_OBJECT(player), "realize", G_CALLBACK(cb_realize),
			NULL);

	vevent = libvlc_media_player_event_manager(media_player);
	libvlc_event_attach(vevent, libvlc_MediaPlayerMediaChanged,
			cb_set_title, window);

	gtk_widget_show_all(window);
	gtk_main();

	libvlc_media_player_release(media_player);
	libvlc_release(vlc_inst);

	exit(EXIT_SUCCESS);
}
