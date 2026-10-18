#include "decide.h"
#include "dialog.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define OBS_SOURCE_AUDIO (1u << 1)
#define OBS_SOURCE_TYPE_INPUT 0
#define OBS_TASK_UI 0
#define LOG_INFO 300

typedef struct obs_source obs_source_t;
typedef struct obs_data obs_data_t;
typedef struct obs_weak_source obs_weak_source_t;
typedef struct signal_handler signal_handler_t;
typedef struct calldata calldata_t;

typedef void (*signal_callback_t)(void *data, calldata_t *cd);
typedef void (*obs_task_t)(void *param);

#define OBS_IMPORT __declspec(dllimport)

OBS_IMPORT void blog(int log_level, const char *format, ...);
OBS_IMPORT bool calldata_get_data(const calldata_t *data, const char *name, void *out, size_t size);
OBS_IMPORT bool obs_data_get_bool(obs_data_t *data, const char *name);
OBS_IMPORT long long obs_data_get_int(obs_data_t *data, const char *name);
OBS_IMPORT void obs_data_release(obs_data_t *data);
OBS_IMPORT void obs_data_set_bool(obs_data_t *data, const char *name, bool val);
OBS_IMPORT void obs_data_set_int(obs_data_t *data, const char *name, long long val);
OBS_IMPORT void obs_enum_sources(bool (*enum_proc)(void *, obs_source_t *), void *param);
OBS_IMPORT signal_handler_t *obs_get_signal_handler(void);
OBS_IMPORT void obs_queue_task(int type, obs_task_t task, void *param, bool wait);
OBS_IMPORT int obs_source_get_monitoring_type(const obs_source_t *source);
OBS_IMPORT const char *obs_source_get_name(const obs_source_t *source);
OBS_IMPORT uint32_t obs_source_get_output_flags(const obs_source_t *source);
OBS_IMPORT obs_data_t *obs_source_get_private_settings(obs_source_t *source);
OBS_IMPORT signal_handler_t *obs_source_get_signal_handler(const obs_source_t *source);
OBS_IMPORT int obs_source_get_type(const obs_source_t *source);
OBS_IMPORT obs_weak_source_t *obs_source_get_weak_source(obs_source_t *source);
OBS_IMPORT bool obs_source_muted(const obs_source_t *source);
OBS_IMPORT void obs_source_release(obs_source_t *source);
OBS_IMPORT void obs_source_set_monitoring_type(obs_source_t *source, int type);
OBS_IMPORT void obs_source_set_muted(obs_source_t *source, bool muted);
OBS_IMPORT const char *obs_source_get_uuid(const obs_source_t *source);
OBS_IMPORT obs_source_t *obs_get_source_by_uuid(const char *uuid);
OBS_IMPORT obs_source_t *obs_weak_source_get_source(obs_weak_source_t *weak);
OBS_IMPORT void obs_weak_source_release(obs_weak_source_t *weak);
OBS_IMPORT void signal_handler_connect(signal_handler_t *handler, const char *signal, signal_callback_t callback,
				       void *data);
OBS_IMPORT void signal_handler_disconnect(signal_handler_t *handler, const char *signal, signal_callback_t callback,
					  void *data);

typedef void (*obs_frontend_cb)(void *private_data);

OBS_IMPORT void obs_frontend_add_tools_menu_item(const char *name, obs_frontend_cb callback, void *private_data);

static void on_tools_menu(void *unused)
{
	(void)unused;
	link_mute_open_dialog();
}

#define HOLD_KEY "link_mute_hold"
#define RESTORE_KEY "link_mute_restore"
#define PREVIEW_KEY "link_mute_preview"

struct hook {
	struct hook *next;
	obs_source_t *source;
	bool queued;
};

static CRITICAL_SECTION hook_lock;
static struct hook *hooks;
static bool alive;
static bool lock_ready;
static int applying;
static int dirty;

static obs_source_t *calldata_source(const calldata_t *data)
{
	void *ptr = NULL;
	if (!data || !calldata_get_data(data, "source", &ptr, sizeof(ptr)))
		return NULL;
	return (obs_source_t *)ptr;
}

static bool is_audio_input(obs_source_t *source)
{
	if (!source)
		return false;
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return false;
	return (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

static struct hook *find_unlocked(obs_source_t *source)
{
	for (struct hook *hook = hooks; hook; hook = hook->next) {
		if (hook->source == source)
			return hook;
	}
	return NULL;
}

static void sync_source(obs_source_t *source);
static void on_destroy(void *unused, calldata_t *cd);

static void run_sync(void *param)
{
	obs_weak_source_t *weak = param;
	obs_source_t *source = obs_weak_source_get_source(weak);
	obs_weak_source_release(weak);
	if (!source)
		return;

	EnterCriticalSection(&hook_lock);
	struct hook *hook = find_unlocked(source);
	if (hook)
		hook->queued = false;
	bool run = alive;
	LeaveCriticalSection(&hook_lock);

	if (run && is_audio_input(source))
		sync_source(source);
	obs_source_release(source);
}

static void schedule(obs_source_t *source)
{
	if (!source || !lock_ready)
		return;

	EnterCriticalSection(&hook_lock);
	if (!alive || applying) {
		if (applying)
			dirty = 1;
		LeaveCriticalSection(&hook_lock);
		return;
	}
	struct hook *hook = find_unlocked(source);
	if (!hook || hook->queued) {
		LeaveCriticalSection(&hook_lock);
		return;
	}
	hook->queued = true;
	obs_weak_source_t *weak = obs_source_get_weak_source(source);
	LeaveCriticalSection(&hook_lock);

	if (!weak) {
		EnterCriticalSection(&hook_lock);
		hook = find_unlocked(source);
		if (hook)
			hook->queued = false;
		LeaveCriticalSection(&hook_lock);
		return;
	}
	obs_queue_task(OBS_TASK_UI, run_sync, weak, false);
}

static void on_changed(void *unused, calldata_t *cd)
{
	(void)unused;
	schedule(calldata_source(cd));
}

static void unhook_signals(obs_source_t *source)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	if (!handler)
		return;
	signal_handler_disconnect(handler, "mute", on_changed, NULL);
	signal_handler_disconnect(handler, "audio_monitoring", on_changed, NULL);
	signal_handler_disconnect(handler, "destroy", on_destroy, NULL);
}

static void on_destroy(void *unused, calldata_t *cd)
{
	(void)unused;
	obs_source_t *source = calldata_source(cd);
	if (!lock_ready)
		return;
	EnterCriticalSection(&hook_lock);
	struct hook **link = &hooks;
	while (*link) {
		if ((*link)->source == source) {
			struct hook *dead = *link;
			*link = dead->next;
			free(dead);
			break;
		}
		link = &(*link)->next;
	}
	LeaveCriticalSection(&hook_lock);
}

static void hook_source(obs_source_t *source)
{
	if (!is_audio_input(source) || !lock_ready)
		return;

	EnterCriticalSection(&hook_lock);
	if (!alive || find_unlocked(source)) {
		LeaveCriticalSection(&hook_lock);
		return;
	}
	struct hook *hook = calloc(1, sizeof(*hook));
	if (!hook) {
		LeaveCriticalSection(&hook_lock);
		return;
	}
	hook->source = source;
	hook->next = hooks;
	hooks = hook;
	LeaveCriticalSection(&hook_lock);

	signal_handler_t *handler = obs_source_get_signal_handler(source);
	signal_handler_connect(handler, "mute", on_changed, NULL);
	signal_handler_connect(handler, "audio_monitoring", on_changed, NULL);
	signal_handler_connect(handler, "destroy", on_destroy, NULL);
	schedule(source);
}

static bool enum_hook(void *unused, obs_source_t *source)
{
	(void)unused;
	hook_source(source);
	return true;
}

static void on_source_created(void *unused, calldata_t *cd)
{
	(void)unused;
	hook_source(calldata_source(cd));
}

static void sync_source(obs_source_t *source)
{
	int allow_only = 0;

	for (;;) {
		obs_data_t *settings = obs_source_get_private_settings(source);
		struct link_input in = {0};
		struct link_output out;
		const char *name;

		in.muted = obs_source_muted(source);
		in.monitoring = obs_source_get_monitoring_type(source);
		if (settings) {
			in.holding = obs_data_get_bool(settings, HOLD_KEY);
			in.saved = (int)obs_data_get_int(settings, RESTORE_KEY);
			in.linked = !obs_data_get_bool(settings, PREVIEW_KEY);
		} else {
			in.linked = 1;
		}
		in.allow_only = allow_only;
		link_decide(&in, &out);

		EnterCriticalSection(&hook_lock);
		dirty = 0;
		applying = 1;
		LeaveCriticalSection(&hook_lock);

		name = obs_source_get_name(source);
		if (!name)
			name = "source";

		if (out.action == LINK_SUPPRESS) {
			if (out.remember_valid && settings) {
				obs_data_set_bool(settings, HOLD_KEY, true);
				obs_data_set_int(settings, RESTORE_KEY, out.remember);
			}
			if (in.monitoring != LINK_MONITOR_NONE)
				obs_source_set_monitoring_type(source, LINK_MONITOR_NONE);
			if (out.mute_output)
				obs_source_set_muted(source, true);
			allow_only = 0;
			blog(LOG_INFO, "[obs-link-mute] muted monitor for '%s'", name);
		} else if (out.action == LINK_RESTORE) {
			if (settings)
				obs_data_set_bool(settings, HOLD_KEY, false);
			allow_only = out.restore == LINK_MONITOR_ONLY;
			obs_source_set_monitoring_type(source, out.restore);
			blog(LOG_INFO, "[obs-link-mute] restored monitor for '%s'", name);
		} else if (out.action == LINK_CLEAR_HOLD) {
			if (settings)
				obs_data_set_bool(settings, HOLD_KEY, false);
			allow_only = 0;
		} else if (out.action == LINK_CLEAR_ALLOW) {
			allow_only = 0;
		}

		if (settings)
			obs_data_release(settings);

		EnterCriticalSection(&hook_lock);
		applying = 0;
		int again = dirty;
		LeaveCriticalSection(&hook_lock);
		if (!again)
			break;
	}
}

static void clear_hold(obs_data_t *settings)
{
	if (!settings)
		return;
	obs_data_set_bool(settings, HOLD_KEY, false);
}

int link_mute_current_choice(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_private_settings(source);
	int type = obs_source_get_monitoring_type(source);
	int choice = LINK_CHOICE_LINKED;
	bool preview = false;
	bool holding = false;
	int saved = LINK_MONITOR_AND_OUTPUT;

	if (settings) {
		preview = obs_data_get_bool(settings, PREVIEW_KEY);
		holding = obs_data_get_bool(settings, HOLD_KEY);
		saved = (int)obs_data_get_int(settings, RESTORE_KEY);
		obs_data_release(settings);
	}
	if (holding && type == LINK_MONITOR_NONE)
		type = saved;

	if (type == LINK_MONITOR_NONE)
		choice = LINK_CHOICE_OFF;
	else if (type == LINK_MONITOR_ONLY)
		choice = LINK_CHOICE_ONLY;
	else if (preview)
		choice = LINK_CHOICE_ENABLED;
	else
		choice = LINK_CHOICE_LINKED;
	return choice;
}

void link_mute_apply_choice(obs_source_t *source, int choice)
{
	obs_data_t *settings;

	if (!source || !lock_ready)
		return;

	EnterCriticalSection(&hook_lock);
	dirty = 0;
	applying = 1;
	LeaveCriticalSection(&hook_lock);

	settings = obs_source_get_private_settings(source);
	if (choice == LINK_CHOICE_OFF) {
		if (settings) {
			obs_data_set_bool(settings, PREVIEW_KEY, true);
			clear_hold(settings);
		}
		obs_source_set_monitoring_type(source, LINK_MONITOR_NONE);
	} else if (choice == LINK_CHOICE_ONLY) {
		if (settings) {
			obs_data_set_bool(settings, PREVIEW_KEY, true);
			clear_hold(settings);
		}
		obs_source_set_monitoring_type(source, LINK_MONITOR_ONLY);
	} else if (choice == LINK_CHOICE_ENABLED) {
		if (settings) {
			obs_data_set_bool(settings, PREVIEW_KEY, true);
			clear_hold(settings);
		}
		obs_source_set_monitoring_type(source, LINK_MONITOR_AND_OUTPUT);
	} else if (obs_source_muted(source)) {
		if (settings) {
			obs_data_set_bool(settings, PREVIEW_KEY, false);
			obs_data_set_bool(settings, HOLD_KEY, true);
			obs_data_set_int(settings, RESTORE_KEY, LINK_MONITOR_AND_OUTPUT);
		}
		obs_source_set_monitoring_type(source, LINK_MONITOR_NONE);
	} else {
		if (settings) {
			obs_data_set_bool(settings, PREVIEW_KEY, false);
			clear_hold(settings);
		}
		obs_source_set_monitoring_type(source, LINK_MONITOR_AND_OUTPUT);
	}
	if (settings)
		obs_data_release(settings);

	EnterCriticalSection(&hook_lock);
	applying = 0;
	LeaveCriticalSection(&hook_lock);
	sync_source(source);
}

__declspec(dllexport) bool obs_module_load(void)
{
	InitializeCriticalSection(&hook_lock);
	lock_ready = true;
	alive = true;

	signal_handler_connect(obs_get_signal_handler(), "source_create", on_source_created, NULL);
	obs_enum_sources(enum_hook, NULL);
	obs_frontend_add_tools_menu_item("Audio Monitoring...", on_tools_menu, NULL);
	blog(LOG_INFO, "[obs-link-mute] loaded; Monitoring Enabled mutes the monitor with the source");
	return true;
}

__declspec(dllexport) void obs_module_unload(void)
{
	link_mute_close_dialog();
	if (lock_ready) {
		EnterCriticalSection(&hook_lock);
		alive = false;
		LeaveCriticalSection(&hook_lock);
	} else {
		alive = false;
	}

	signal_handler_t *core = obs_get_signal_handler();
	if (core)
		signal_handler_disconnect(core, "source_create", on_source_created, NULL);

	if (lock_ready) {
		EnterCriticalSection(&hook_lock);
		while (hooks) {
			struct hook *hook = hooks;
			hooks = hook->next;
			obs_source_t *source = hook->source;
			free(hook);
			LeaveCriticalSection(&hook_lock);
			if (source)
				unhook_signals(source);
			EnterCriticalSection(&hook_lock);
		}
		LeaveCriticalSection(&hook_lock);
		DeleteCriticalSection(&hook_lock);
		lock_ready = false;
	}
	blog(LOG_INFO, "[obs-link-mute] unloaded");
}

__declspec(dllexport) void obs_module_set_pointer(void *module)
{
	(void)module;
}

__declspec(dllexport) uint32_t obs_module_ver(void)
{
	return (32u << 24) | (2u << 16) | 2u;
}

__declspec(dllexport) const char *obs_module_name(void)
{
	return "Link Mute";
}

__declspec(dllexport) const char *obs_module_description(void)
{
	return "Mutes audio monitoring together with the source mute.";
}

__declspec(dllexport) const char *obs_module_author(void)
{
	return "obs-link-mute";
}
