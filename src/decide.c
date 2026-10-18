#include "decide.h"

void link_decide(const struct link_input *in, struct link_output *out)
{
	out->action = LINK_KEEP;
	out->remember = 0;
	out->remember_valid = 0;
	out->mute_output = 0;
	out->restore = 0;

	if (!in)
		return;

	/* Preview and Monitor Off leave the headphones alone, and undo a forced mute. */
	if (!in->linked) {
		if (in->holding && in->monitoring == LINK_MONITOR_NONE) {
			out->action = LINK_RESTORE;
			if (in->saved == LINK_MONITOR_ONLY || in->saved == LINK_MONITOR_AND_OUTPUT)
				out->restore = in->saved;
			else
				out->restore = LINK_MONITOR_AND_OUTPUT;
		} else if (in->holding) {
			out->action = LINK_CLEAR_HOLD;
		}
		return;
	}

	/*
	 * Restoring "monitor only" leaves output silent on purpose.
	 * The next sync, caused by that restore, must not treat it as a mute.
	 */
	if (in->allow_only && !in->muted && in->monitoring == LINK_MONITOR_ONLY) {
		out->action = LINK_CLEAR_ALLOW;
		return;
	}

	/* "Monitoring Enabled" only. Monitor Only stays a preview. */
	if (in->muted && in->monitoring == LINK_MONITOR_AND_OUTPUT) {
		out->action = LINK_SUPPRESS;
		if (!in->holding) {
			out->remember_valid = 1;
			out->remember = LINK_MONITOR_AND_OUTPUT;
		}
		return;
	}

	if (!in->muted && in->holding && in->monitoring == LINK_MONITOR_NONE) {
		out->action = LINK_RESTORE;
		if (in->saved == LINK_MONITOR_ONLY || in->saved == LINK_MONITOR_AND_OUTPUT)
			out->restore = in->saved;
		else
			out->restore = LINK_MONITOR_AND_OUTPUT;
		return;
	}

	if (!in->muted && in->holding)
		out->action = LINK_CLEAR_HOLD;
}
