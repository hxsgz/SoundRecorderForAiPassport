#include "../firmware/main/gate.h"

size_t test_gate_size(void) { return sizeof(gate_t); }
void test_reset_votes(gate_t *gate) { gate->votes = 0; }
int test_continuing(gate_t *gate) { return gate->continuing; }
