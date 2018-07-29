#include <stdint.h>
#include <stdio.h>
#include <vector>

struct counter_state
{
	uint16_t boosted_counters[2];
	uint16_t next_state[2];
};

int counter_to_state[256][256];

static int CounterVisit(std::vector<counter_state>& states, unsigned char c0, unsigned char c1, int next_bit, bool saturate)
{
	if(next_bit == 0)
	{
		if(!saturate || c0 < 255) c0++;
		if(c1 > 1) c1 >>= 1;
	}
	else
	{
		if (!saturate || c1 < 255) c1++;
		if(c0 > 1) c0 >>= 1;
	}

	if(counter_to_state[c1][c0] != -1)
		return counter_to_state[c1][c0];

	int boost = (c0 == 0 || c1 == 0) ? 2 : 0;

	int state_idx = states.size();
	counter_to_state[c1][c0] = state_idx;
	
	counter_state s;
	s.boosted_counters[0] = c0 << boost;
	s.boosted_counters[1] = c1 << boost;
	states.push_back(s);
	states[state_idx].next_state[0] = CounterVisit(states, c0, c1, 0, saturate);
	states[state_idx].next_state[1] = CounterVisit(states, c0, c1, 1, saturate);
	return state_idx;
}

static void GenerateCounterStates(std::vector<counter_state>& states, bool saturate)
{
	states.reserve(2048);
	for(int i = 0; i < 256; i++)
		for(int j = 0; j < 256; j++)
			counter_to_state[i][j] = -1;

	counter_to_state[0][1] = 0;
	counter_to_state[1][0] = 1;


	counter_state s0;
	s0.boosted_counters[0] = 1 << 2;
	s0.boosted_counters[1] = 0;
	states.push_back(s0);
	counter_state s1;
	s1.boosted_counters[0] = 0;
	s1.boosted_counters[1] = 1 << 2;
	states.push_back(s1);

	states[0].next_state[0] = CounterVisit(states, 1, 0, 0, saturate);
	states[0].next_state[1] = CounterVisit(states, 1, 0, 1, saturate);
	states[1].next_state[0] = CounterVisit(states, 0, 1, 0, saturate);
	states[1].next_state[1] = CounterVisit(states, 0, 1, 1, saturate);
}

static void PrintCounters(const char* name, std::vector<counter_state>& states)
{
	int n = states.size();
	int column = 4;
	printf("counter_state %s[%d] = {", name, states.size());
	for (int i = 0; i < n; i++) {
		if (i % column == 0)
			printf("\n    ");
		printf("{%4d, %4d, %4d, %4d }, ", states[i].boosted_counters[0], states[i].boosted_counters[1], states[i].next_state[0], states[i].next_state[1]);
	}
	printf("\n};\n");
}

int main(int argc, const char* argv[])
{
	std::vector<counter_state> unsaturated_counter_states;
	GenerateCounterStates(unsaturated_counter_states, false);
	PrintCounters("unsaturated_counter_states", unsaturated_counter_states);
	printf("\n");
	std::vector<counter_state> saturated_counter_states;
	GenerateCounterStates(saturated_counter_states, true);
	PrintCounters("saturated_counter_states", saturated_counter_states);
	return 0;
}