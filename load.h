#include "common.h"
struct Range
{
	TADDR LowAddress;
	TADDR HighAddress;
};

class LoadSettings
{
public:
	LoadSettings();

	void SetSeed(unsigned int new_seed)
	{
		seed = new_seed;
	}

	unsigned int GetSeed();

	void SetDeleteDensity(int delete_density)
	{
		delete_divisor = (delete_density>100)?100:delete_density;
		delete_divisor = (delete_divisor<1)?1:delete_divisor;
		delete_divisor = 101 - delete_divisor;
	}

	int GetDeleteDivisor(){return delete_divisor;};

	void OrderAscending()
	{
		ascending = true;
		descending = false;
		random = false;
	}

	void OrderDescending()
	{
		ascending = false;
		descending = true;
		random = false;
	}

	void OrderRandom()
	{
		ascending = false;
		descending = false;
		random = true;
	}

	void SetReadLoadDensity(int new_read_load_density)
	{
		read_load_density = new_read_load_density;
		if (read_load_density < 1)
			read_load_density = 1;
	}

	int GetReadLoadDensity() {return read_load_density;};

	bool IsRandom() {return random;}
	bool IsAscending() {return ascending;}
	bool IsDescending() {return descending;}
	bool descending;
	bool ascending;
	bool random;
	unsigned int seed;
	bool seed_is_set;

	int read_load_density;

	int delete_divisor;
};

