#pragma once
#ifndef _COUNTER_STATE_
#define _COUNTER_STATE_

struct CounterState
{
	unsigned short boosted_counters[2];
	unsigned short next_state[2];
};

extern CounterState unsaturated_counter_states[];
extern CounterState saturated_counter_states[];

#endif