#pragma once

/* Monitoring values match obs_monitoring_type in OBS 32. */
#define LINK_MONITOR_NONE 0
#define LINK_MONITOR_ONLY 1
#define LINK_MONITOR_AND_OUTPUT 2

enum link_action {
	LINK_KEEP = 0,
	LINK_SUPPRESS = 1,
	LINK_RESTORE = 2,
	LINK_CLEAR_HOLD = 3,
	LINK_CLEAR_ALLOW = 4,
};

enum link_choice {
	LINK_CHOICE_OFF = 0,
	LINK_CHOICE_ONLY = 1,
	LINK_CHOICE_ENABLED = 2,
	LINK_CHOICE_LINKED = 3,
};

struct link_input {
	int muted;
	int monitoring;
	int holding;
	int saved;
	int allow_only;
	int linked;
};

struct link_output {
	int action;
	int remember;
	int remember_valid;
	int mute_output;
	int restore;
};

void link_decide(const struct link_input *in, struct link_output *out);
