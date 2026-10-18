#include "decide.h"

#include <stdio.h>

static int failures;

static struct link_output decide(int muted, int monitoring, int holding, int saved, int allow_only, int linked)
{
	struct link_input in = {muted, monitoring, holding, saved, allow_only, linked};
	struct link_output out;
	link_decide(&in, &out);
	return out;
}

static void expect_action(const char *name, struct link_output out, int action)
{
	if (out.action != action) {
		fprintf(stderr, "%s: action %d, expected %d\n", name, out.action, action);
		failures++;
	}
}

int main(void)
{
	struct link_output out;

	out = decide(0, LINK_MONITOR_AND_OUTPUT, 0, 0, 0, 1);
	expect_action("live monitoring enabled", out, LINK_KEEP);

	out = decide(0, LINK_MONITOR_NONE, 0, 0, 0, 1);
	expect_action("monitor off", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_NONE, 0, 0, 0, 1);
	expect_action("muted, monitor already off", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_NONE, 1, LINK_MONITOR_AND_OUTPUT, 0, 1);
	expect_action("held mute stays quiet", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_AND_OUTPUT, 0, 0, 0, 1);
	expect_action("mute while monitoring enabled", out, LINK_SUPPRESS);
	if (!out.remember_valid || out.remember != LINK_MONITOR_AND_OUTPUT || out.mute_output) {
		fprintf(stderr, "mute while monitoring enabled: remember flags wrong\n");
		failures++;
	}

	out = decide(0, LINK_MONITOR_ONLY, 0, 0, 0, 1);
	expect_action("monitor only stays preview", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_ONLY, 0, 0, 0, 1);
	expect_action("muted monitor only stays preview", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_AND_OUTPUT, 0, 0, 0, 0);
	expect_action("preview mode ignores mute", out, LINK_KEEP);

	out = decide(1, LINK_MONITOR_NONE, 1, LINK_MONITOR_AND_OUTPUT, 0, 0);
	expect_action("leaving linked mode restores monitor while muted", out, LINK_RESTORE);
	if (out.restore != LINK_MONITOR_AND_OUTPUT) {
		fprintf(stderr, "preview restore type %d\n", out.restore);
		failures++;
	}

	out = decide(0, LINK_MONITOR_NONE, 1, LINK_MONITOR_AND_OUTPUT, 0, 1);
	expect_action("unmute restores monitoring enabled", out, LINK_RESTORE);
	if (out.restore != LINK_MONITOR_AND_OUTPUT) {
		fprintf(stderr, "unmute restore type %d\n", out.restore);
		failures++;
	}

	out = decide(0, LINK_MONITOR_NONE, 1, LINK_MONITOR_ONLY, 0, 1);
	expect_action("unmute restores monitor only", out, LINK_RESTORE);
	if (out.restore != LINK_MONITOR_ONLY) {
		fprintf(stderr, "monitor-only restore type %d\n", out.restore);
		failures++;
	}

	out = decide(0, LINK_MONITOR_NONE, 1, 0, 0, 1);
	expect_action("missing saved mode", out, LINK_RESTORE);
	if (out.restore != LINK_MONITOR_AND_OUTPUT) {
		fprintf(stderr, "fallback restore type %d\n", out.restore);
		failures++;
	}

	out = decide(0, LINK_MONITOR_ONLY, 0, 0, 1, 1);
	expect_action("ignore restored monitor only", out, LINK_CLEAR_ALLOW);

	out = decide(0, LINK_MONITOR_AND_OUTPUT, 1, LINK_MONITOR_AND_OUTPUT, 0, 1);
	expect_action("stale hold while already live", out, LINK_CLEAR_HOLD);

	if (failures) {
		fprintf(stderr, "%d failed\n", failures);
		return 1;
	}
	printf("link-mute decisions ok\n");
	return 0;
}
