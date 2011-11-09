
#pragma once

class PatternDB {
	static const uint64_t empty = 0xFFFFFFFFFFFFFFFF;
	struct Entry {
		uint64_t pattern;
		float    gamma;
		Entry(uint64_t p = empty, float g = 0) : pattern(p), gamma(g) { }
	};

	float default_val; //value to return when no value is found
	unsigned int size; //how many slots
	unsigned int num;  //how many are used
	uint64_t mask;
	Entry * table;

public:
	PatternDB() : default_val(1), size(0), num(0), mask(0), table(NULL) { }
	PatternDB(unsigned int s){ init(s); }
	~PatternDB(){
		clear();
	}

	void init(unsigned int s){
		clear();
		size = roundup(s)*4;
		num = 0;
		mask = size-1;
		table = new Entry[size];
	}

	void clear(){
		if(table)
			delete[] table;
		table = NULL;
	}

	void clean(){
		for(uintptr_t i = 0; i < size; i++)
			table[i] = Entry();
		num = 0;
	}

	void set_default(float gamma){
		default_val = gamma;
	}

	void set(uint64_t pattern, float gamma){
		uint64_t i = mix_bits(pattern) & mask;
		while(table[i].pattern != empty)
			i = (i+1) & mask;
		table[i] = Entry(pattern, gamma);
		num++;
	}

	const float & operator[](uint64_t pattern) const {
		for(uint64_t i = mix_bits(pattern) & mask; table[i].pattern != empty; i = (i+1) & mask)
			if(table[i].pattern == pattern)
				return table[i].gamma;
		return default_val;
	}

	//from murmurhash
	static uint64_t mix_bits(uint64_t h){
		h ^= h >> 33;
		h *= 0xff51afd7ed558ccd;
		h ^= h >> 33;
		h *= 0xc4ceb9fe1a85ec53;
		h ^= h >> 33;
		return h;
	}

	//round a number up to the nearest power of 2
	static unsigned int roundup(unsigned int v) {
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}
};
