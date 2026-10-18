#pragma once

struct obs_source;

void link_mute_open_dialog(void);
void link_mute_close_dialog(void);

int link_mute_current_choice(struct obs_source *source);
void link_mute_apply_choice(struct obs_source *source, int choice);
