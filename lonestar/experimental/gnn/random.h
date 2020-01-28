#ifndef RANDOM_H
#define RANDOM_H
//#include <boost/shared_ptr.hpp>

typedef boost::mt19937 rng_t;

// random seeding
int64_t seedgen(void) {
	int64_t s, seed, pid;
	FILE* f = fopen("/dev/urandom", "rb");
	if (f && fread(&seed, 1, sizeof(seed), f) == sizeof(seed)) {
		fclose(f);
		return seed;
	}
	std::cout << "System entropy source not available, using fallback algorithm to generate seed instead.";
	if (f) fclose(f);
	pid = getpid();
	s = time(NULL);
	seed = std::abs(((s * 181) * ((pid - 83) * 359)) % 104729);
	return seed;
}

// This random number generator facade hides boost and CUDA rng
// implementation from one another (for cross-platform compatibility).
class RNG {
public:
	RNG() : generator_(new Generator()) { }
	explicit RNG(unsigned int seed) : generator_(new Generator(seed)) { }
	explicit RNG(const RNG&);
	RNG& operator=(const RNG& other) { generator_ = other.generator_; return *this; }
	void* generator() { return static_cast<void*>(generator_->rng()); }
private:
	class Generator {
		public:
			Generator() : rng_(new rng_t(seedgen())) {}
			explicit Generator(unsigned seed) : rng_(new rng_t(seed)) {}
			rng_t* rng() { return rng_.get(); }
		private:
			std::shared_ptr<rng_t> rng_;
	};

	std::shared_ptr<Generator> generator_;
};

std::shared_ptr<RNG> random_generator_;
inline static RNG& rng_stream() {
	random_generator_.reset(new RNG());
	return *random_generator_;
}

inline rng_t* rng() {
	return static_cast<rng_t*>(rng_stream().generator());
}

#endif
