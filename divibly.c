/*
 * divibly - A simple DVB-T viewer
 *
 * Copyright (C) 2014 - 2015, 2017	Andrew Clayton
 * 					<andrew@digital-domain.net>
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

#define WINDOW_W	1024
#define WINDOW_H	 576

#define OSD_FONT	"-adobe-utopia-bold-r-normal--96-0-0-0-p-0-iso8859-1"
#define OSD_TIMEOUT	2

#define BUF_SIZE	4096
#define CHAN_ALLOC_SZ	  25

enum { CHAN_NAME = 0, CHAN_IDX };

static int nr_channels;
static xosd *osd_display;
static timer_t osd_timerid;

struct divibly {
	libvlc_media_player_t *media_player;
	libvlc_instance_t *vlc_inst;

	int chan_idx;

	bool fullscreen;

	GtkWidget *window;
	GtkWidget *player;
	GtkWidget *box;
	GtkWidget *chan_srch;
	GtkEntryCompletion *completion;
	GtkListStore *liststore;
};

struct chan_info {
	char *name;
	unsigned int freq;
	unsigned int bandwidth;
	unsigned int pid;
};

static struct chan_info *channels;

static void free_channels(void)
{
	int i;

	for (i = 0; i < nr_channels; i++)
		free(channels[i].name);
	free(channels);
}

static void toggle_fullscreen(struct divibly *divibly)
{
	if (!divibly->fullscreen)
		gtk_window_fullscreen(GTK_WINDOW(divibly->window));
	else
		gtk_window_unfullscreen(GTK_WINDOW(divibly->window));

	divibly->fullscreen = !divibly->fullscreen;
}

static void kill_osd(int sig)
{
	timer_delete(osd_timerid);

	if (!osd_display)
		return;

	xosd_destroy(osd_display);
	osd_display = NULL;
}

static void set_osd_timer(void)
{
	struct itimerspec its;
	struct sigevent sev;
	struct sigaction action;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = kill_osd;
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

static void set_osd(const char *msg)
{
	if (osd_timerid) {
		timer_delete(osd_timerid);
		xosd_destroy(osd_display);
	}

	osd_display = xosd_create(1);
	xosd_set_font(osd_display, OSD_FONT);
	xosd_set_colour(osd_display, "white");

	xosd_display(osd_display, 0, XOSD_string, msg);
	set_osd_timer();
}

static void set_spu(struct divibly *divibly)
{
	static int spu = -1;
	libvlc_track_description_t *desc;

	/* Toggle subtitles off */
	if (spu > -1) {
		spu = -1;
		set_osd("Subtitles: Off");
		goto out;
	}

	/* Find the subtitle id */
	desc = libvlc_video_get_spu_description(divibly->media_player);
	while (desc) {
		spu = desc->i_id;
		if (spu > -1)
			break;
		desc = desc->p_next;
	}

	if (spu == -1)
		set_osd("Subtitles: None");
	else
		set_osd("Subtitles: On");

out:
	libvlc_video_set_spu(divibly->media_player, spu);
}

static void get_channel_info(const char *channels_conf,
			     GtkListStore *liststore)
{
	char buf[BUF_SIZE];
	int i = 0;
	FILE *fp;
	size_t ci_sz = sizeof(struct chan_info);

	fp = fopen(channels_conf, "r");
	while (fgets(buf, BUF_SIZE, fp)) {
		GtkTreeIter iter;

		if (i % CHAN_ALLOC_SZ == 0)
			channels = realloc(channels,
					i * ci_sz + CHAN_ALLOC_SZ * ci_sz);
		sscanf(buf, "%m[^:]:%u:%*m[^:]:BANDWIDTH_%u_MHZ:%*m[^:]:"
				"%*m[^:]:%*m[^:]:%*m[^:]:%*m[^:]:%*m[^:]:"
				"%*u:%*u:%u",
				&channels[i].name, &channels[i].freq,
				&channels[i].bandwidth, &channels[i].pid);
		gtk_list_store_append(liststore, &iter);
		gtk_list_store_set(liststore, &iter, CHAN_NAME,
				channels[i].name, CHAN_IDX, i, -1);
		i++;
	}
	nr_channels = i;

	fclose(fp);
}

static void play_channel(struct divibly *divibly)
{
	libvlc_media_t *media;
	char f_opt[64];
	char b_opt[64];
	char p_opt[64];

	if (divibly->chan_idx >= nr_channels)
		divibly->chan_idx = 0;
	else if (divibly->chan_idx < 0)
		divibly->chan_idx = nr_channels - 1;

	snprintf(f_opt, sizeof(f_opt), ":dvb-frequency=%u",
			channels[divibly->chan_idx].freq);
	snprintf(b_opt, sizeof(b_opt), ":dvb-bandwidth=%u",
			channels[divibly->chan_idx].bandwidth);
	snprintf(p_opt, sizeof(p_opt), ":program=%u",
			channels[divibly->chan_idx].pid);

	media = libvlc_media_new_location(divibly->vlc_inst, "dvb://");
	libvlc_media_add_option(media, f_opt);
	libvlc_media_add_option(media, b_opt);
	libvlc_media_add_option(media, p_opt);
	libvlc_media_player_set_media(divibly->media_player, media);
	libvlc_media_release(media);

	libvlc_media_player_play(divibly->media_player);
}

static gboolean cb_inputw(GtkWidget *player, GdkEventKey *event,
			  struct divibly *divibly)
{
	if (!gtk_widget_has_focus(divibly->chan_srch))
		return FALSE;

	switch (event->keyval) {
	case GDK_KEY_Escape:
		/* cancel channel search */
		gtk_widget_hide(divibly->chan_srch);
		gtk_entry_set_text(GTK_ENTRY(divibly->chan_srch), "");
		gtk_widget_grab_focus(divibly->player);

		return TRUE;
	}

	/* Allow cb_input to process events */
	return FALSE;
}

static gboolean cb_input_keyb(GtkWidget *player, GdkEventKey *event,
			      struct divibly *divibly)
{
	switch (event->keyval) {
	case GDK_KEY_f:
	case GDK_KEY_F:
		toggle_fullscreen(divibly);
		break;
	case GDK_KEY_z:
	case GDK_KEY_Z:
		gtk_window_resize(GTK_WINDOW(divibly->window), WINDOW_W,
				WINDOW_H);
		break;
	case GDK_KEY_r:
	case GDK_KEY_R:
		play_channel(divibly);
		break;
	case GDK_KEY_0:
		divibly->chan_idx = 9;
		play_channel(divibly);
		break;
	case GDK_KEY_1 ... GDK_KEY_9:
		divibly->chan_idx = event->keyval - GDK_KEY_1;
		play_channel(divibly);
		break;
	case GDK_KEY_Up:
		divibly->chan_idx++;
		play_channel(divibly);
		break;
	case GDK_KEY_Down:
		divibly->chan_idx--;
		play_channel(divibly);
		break;
	case GDK_KEY_s:
	case GDK_KEY_S:
		set_spu(divibly);
		break;
	case GDK_KEY_slash:
		gtk_widget_grab_focus(divibly->chan_srch);
		gtk_widget_show(divibly->chan_srch);
		break;
	case GDK_KEY_m:
	case GDK_KEY_M: {
		bool mute;

		libvlc_audio_toggle_mute(divibly->media_player);
		mute = libvlc_audio_get_mute(divibly->media_player);
		if (!mute)
			set_osd("Mute: on");
		else
			set_osd("Mute: off");
		break;
	}
	case GDK_KEY_q:
	case GDK_KEY_Q:
		gtk_main_quit();
		break;
	case GDK_KEY_Escape:
		if (divibly->fullscreen)
			toggle_fullscreen(divibly);
		else
			gtk_main_quit();
	}

	return TRUE;
}

static gboolean cb_input_mouse(GtkWidget *player, GdkEventKey *event,
			       struct divibly *divibly)
{
	if (event->type == GDK_2BUTTON_PRESS)
		toggle_fullscreen(divibly);

	return TRUE;
}

static gboolean goto_channel(GtkEntryCompletion *widget, GtkTreeModel *model,
			     GtkTreeIter *iter, struct divibly *divibly)
{
	char *chan;

	gtk_tree_model_get(model, iter, CHAN_NAME, &chan, CHAN_IDX,
			&divibly->chan_idx, -1);
	gtk_entry_set_text(GTK_ENTRY(divibly->chan_srch), chan);
	g_free(chan);
	play_channel(divibly);
	gtk_widget_hide(divibly->chan_srch);
	gtk_entry_set_text(GTK_ENTRY(divibly->chan_srch), "");
	gtk_widget_grab_focus(divibly->player);

	return TRUE;
}

static void cb_realize(GtkWidget *widget, struct divibly *divibly)
{
	libvlc_media_player_set_xwindow(divibly->media_player, GDK_WINDOW_XID(
				gtk_widget_get_window(widget)));

	libvlc_video_set_deinterlace(divibly->media_player, "yadif2x");

	play_channel(divibly);
}

static void cb_set_title(const struct libvlc_event_t *ev, void *data)
{
	struct divibly *divibly = data;
	GtkWindow *window = GTK_WINDOW(divibly->window);
	char title[255];

	snprintf(title, sizeof(title), "divibly (%s)",
			channels[divibly->chan_idx].name);
	gtk_window_set_title(window, title);

	if (osd_timerid) {
		timer_delete(osd_timerid);
		xosd_destroy(osd_display);
	}

	osd_display = xosd_create(1);
	xosd_set_font(osd_display, OSD_FONT);
	xosd_set_colour(osd_display, "white");
	xosd_display(osd_display, 0, XOSD_string,
			channels[divibly->chan_idx].name);

	set_osd_timer();
}

int main(int argc, char *argv[])
{
	struct divibly *divibly;
	const char *libvlc_argv[] = { "--no-xlib", (const char *)NULL };
	libvlc_event_manager_t *vevent;

	if (argc < 2) {
		fprintf(stderr, "Usage: divibly /path/to/channels.conf\n");
		exit(EXIT_FAILURE);
	}

	gtk_init(&argc, &argv);

	divibly = g_slice_new(struct divibly);

	/* Create the main window */
	divibly->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(divibly->window, "destroy", G_CALLBACK(gtk_main_quit),
			NULL);
	g_signal_connect(divibly->window, "key_press_event",
			G_CALLBACK(cb_inputw), divibly);
	gtk_container_set_border_width(GTK_CONTAINER(divibly->window), 0);
	gtk_window_set_default_size(GTK_WINDOW(divibly->window), WINDOW_W,
			WINDOW_H);
	gtk_window_fullscreen(GTK_WINDOW(divibly->window));
	gtk_window_set_title(GTK_WINDOW(divibly->window), "divibly");

	divibly->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_set_homogeneous(GTK_BOX(divibly->box), FALSE);
	gtk_container_add(GTK_CONTAINER(divibly->window), divibly->box);

	/* Create the container for the video */
	divibly->player = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(divibly->box), divibly->player, TRUE, TRUE,
			0);
	gtk_widget_set_can_focus(divibly->player, TRUE);
	gtk_widget_grab_focus(divibly->player);
	gtk_widget_add_events(divibly->player, GDK_BUTTON_PRESS_MASK);

	/* Create a List Store to hold channel name and index */
	divibly->liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	get_channel_info(argv[1], divibly->liststore);

	/* Create a text entry/completion for doing channel searches */
	divibly->chan_srch = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(divibly->box), divibly->chan_srch, FALSE,
			FALSE, 0);
	divibly->completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(divibly->completion,
			GTK_TREE_MODEL(divibly->liststore));
	gtk_entry_completion_set_text_column(divibly->completion, CHAN_NAME);
	gtk_entry_set_completion(GTK_ENTRY(divibly->chan_srch),
			divibly->completion);
	g_signal_connect(G_OBJECT(divibly->completion), "match-selected",
			G_CALLBACK(goto_channel), divibly);
	g_object_unref(divibly->completion);
	g_signal_connect(divibly->player, "key_press_event",
			G_CALLBACK(cb_input_keyb), divibly);
	g_signal_connect(divibly->player, "button_press_event",
			G_CALLBACK(cb_input_mouse), divibly);

	/* Setup the VLC side of things */
	divibly->vlc_inst = libvlc_new(1, libvlc_argv);
	libvlc_set_user_agent(divibly->vlc_inst, "divibly", NULL);

	divibly->media_player = libvlc_media_player_new(divibly->vlc_inst);
	g_signal_connect(G_OBJECT(divibly->player), "realize",
			G_CALLBACK(cb_realize), divibly);

	vevent = libvlc_media_player_event_manager(divibly->media_player);
	libvlc_event_attach(vevent, libvlc_MediaPlayerMediaChanged,
			cb_set_title, divibly);

	divibly->chan_idx = 0;
	divibly->fullscreen = true;
	gtk_widget_show_all(divibly->window);
	gtk_widget_hide(divibly->chan_srch);
	gtk_main();

	libvlc_media_player_release(divibly->media_player);
	libvlc_release(divibly->vlc_inst);
	g_slice_free(struct divibly, divibly);
	free_channels();

	exit(EXIT_SUCCESS);
}
