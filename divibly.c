/*
 * divibly - A simple DVB-T viewer
 *
 * Copyright (C) 2014		Andrew Clayton <andrew@digital-domain.net>
 *
 * Licensed under the GNU General Public License V2
 * See COPYING
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include <vlc/vlc.h>

#define MAX_CHANNELS	 255
#define BANDWIDTH_MHZ	   8

#define BUF_SIZE	4096

static libvlc_media_player_t *media_player;
static libvlc_instance_t *vlc_inst;
static bool fullscreen;
static int nr_channels;
static int chan_idx;

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

static void play_channel(int chan_idx)
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
		if (!fullscreen) {
			gtk_window_fullscreen(GTK_WINDOW(window));
			fullscreen = true;
		} else {
			gtk_window_unfullscreen(GTK_WINDOW(window));
			fullscreen = false;
		}
		break;
	case GDK_KEY_0:
		play_channel(9);
		break;
	case GDK_KEY_1 ... GDK_KEY_9:
		play_channel(event->keyval - GDK_KEY_1);
		break;
	case GDK_KEY_Up:
		play_channel(--chan_idx);
		break;
	case GDK_KEY_Down:
		play_channel(++chan_idx);
		break;
	case GDK_KEY_q:
	case GDK_KEY_Escape:
		gtk_main_quit();
	}
}

static void cb_realize(GtkWidget *widget, gpointer data)
{
	libvlc_media_player_set_xwindow(media_player, GDK_WINDOW_XID(
				gtk_widget_get_window(widget)));

	libvlc_video_set_deinterlace(media_player, "yadiff2x");

	play_channel(chan_idx);
}

static void cb_set_size(const struct libvlc_event_t *ev, void *data)
{
	int err;
	unsigned int w;
	unsigned int h;
	GtkWindow *window = GTK_WINDOW(data);

	err = libvlc_video_get_size(media_player, 0, &w, &h);
	if (err || !w || !h)
		gtk_window_resize(window, 1024, 576);
	else
		gtk_window_resize(window, w, h);
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
	libvlc_event_attach(vevent, libvlc_MediaPlayerVout, cb_set_size,
			window);

	gtk_widget_show_all(window);
	gtk_main();

	libvlc_media_player_release(media_player);
	libvlc_release(vlc_inst);

	exit(EXIT_SUCCESS);
}
