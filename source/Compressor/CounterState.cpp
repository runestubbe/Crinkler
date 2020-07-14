#include "CounterState.h"

#include <memory>
#include <cassert>

CounterState unsaturated_counter_states[1471];
CounterState saturated_counter_states[1470];

static int counter_to_state[256][256];

static int CounterVisit(CounterState* states, int& next_state, int max_states, unsigned char c0, unsigned char c1, int next_bit, bool saturate)
{
	if (next_bit == 0)
	{
		if (!saturate || c0 < 255) c0++;
		if (c1 > 1) c1 >>= 1;
	}
	else
	{
		if (!saturate || c1 < 255) c1++;
		if (c0 > 1) c0 >>= 1;
	}

	if (counter_to_state[c1][c0] != -1)
		return counter_to_state[c1][c0];

	int boost = (c0 == 0 || c1 == 0) ? 2 : 0;

	int state_idx = next_state;
	counter_to_state[c1][c0] = state_idx;

	assert(next_state < max_states);
	CounterState& s = states[next_state++];
	s.boosted_counters[0] = c0 << boost;
	s.boosted_counters[1] = c1 << boost;

	states[state_idx].next_state[0] = CounterVisit(states, next_state, max_states, c0, c1, 0, saturate);
	states[state_idx].next_state[1] = CounterVisit(states, next_state, max_states, c0, c1, 1, saturate);
	return state_idx;
}

static void GenerateCounterStates(CounterState* states, int max_states, bool saturate)
{
	memset(counter_to_state, -1, sizeof(counter_to_state));
	
	counter_to_state[0][1] = 0;
	counter_to_state[1][0] = 1;

	int next_state = 0;

	assert(next_state < max_states);
	CounterState& s0 = states[next_state++];
	s0.boosted_counters[0] = 1 << 2;
	s0.boosted_counters[1] = 0;

	assert(next_state < max_states);
	CounterState& s1 = states[next_state++];
	s1.boosted_counters[0] = 0;
	s1.boosted_counters[1] = 1 << 2;

	states[0].next_state[0] = CounterVisit(states, next_state, max_states, 1, 0, 0, saturate);
	states[0].next_state[1] = CounterVisit(states, next_state, max_states, 1, 0, 1, saturate);
	states[1].next_state[0] = CounterVisit(states, next_state, max_states, 0, 1, 0, saturate);
	states[1].next_state[1] = CounterVisit(states, next_state, max_states, 0, 1, 1, saturate);
	assert(next_state == max_states);
}

void InitCounterStates()
{
	GenerateCounterStates(unsaturated_counter_states,	sizeof(unsaturated_counter_states) / sizeof(unsaturated_counter_states[0]),	false);
	GenerateCounterStates(saturated_counter_states,		sizeof(saturated_counter_states) / sizeof(saturated_counter_states[0]),		true);
}