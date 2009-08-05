#include <iostream>

#include "random.hpp"

namespace rng {

static unsigned int next = 1;

int generate() {
	return 1;
	next = next * 1103515245 + 12345;
	const int result = ((unsigned int)(next/65536) % 32768);
	std::cerr << "RANDOM GENERATE: " << result << "\n";
	return result;
}

void set_seed(unsigned int seed) {
	std::cerr << "RANDOM SEED: " << seed << "\n";
	next = seed;
}

unsigned int get_seed() {
	return next;
}

}
