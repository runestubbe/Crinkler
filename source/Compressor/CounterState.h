#pragma once
#ifndef _COUNTER_STATE_
#define _COUNTER_STATE_

struct counter_state
{
	unsigned short boosted_counters[2];
	unsigned short next_state[2];
};

extern counter_state unsaturated_counter_states[];
extern counter_state saturated_counter_states[];

#endif