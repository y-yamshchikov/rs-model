
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "load.h"
//#include <time.h>
#include <vector>
//#define PRINTF printf
#define PRINTF(...) 
#include "codeman.h"

using namespace std;

void debug_trap(void)
{
	while(true)
		sleep(1);
}

LoadSettings::LoadSettings()
{
	seed_is_set = false;
	SetReadLoadDensity(5);
	//OrderRandom();
	//OrderAscending();
	OrderDescending();
	SetDeleteDensity(10);//just default value, no special meaning
}

unsigned int LoadSettings::GetSeed()
{
	if (seed_is_set)
	{
		return seed;
	}
	else
	{
		time_t t;
		seed = (unsigned int) time(&t);
		seed_is_set = true;
		return seed;
	}
}

int main(int argc, char* argv[])
{
	const int epochs = 1000;	
	const int elems = 1000;	

	LoadSettings load_settings;
	load_settings.OrderRandom();


	if (argc > 1)
	{
		load_settings.SetSeed(atoi(argv[1]));
	}
	srand(load_settings.GetSeed());


	ExecutionManager *EM = new ExecutionManager();
	EM->Init();

	for (int i = 0; i < epochs; ++i)
	{
		//EM->DumpReaderArray();
		EM->Reinit(); //pretend we have new EM instance

		printf("i=%d\n", i);
		fflush(stdout);
		vector<unsigned int>  nums;
		vector<Range> ranges;
		vector<int> deleted_ranges;

		EM->Reinit();

		for (int n = 0; n < elems*2; n++)
		{
			unsigned int rnd;
			bool f = false;
			do
			{
				f = false;
				rnd = (rand()<<1) + rand()%2;
				rnd = rnd<<16;
				for (auto x : nums)
				{
					if( rnd == x)
						f = true;
				} 
			}while ( f );
			nums.push_back(rnd);
		}

		if (load_settings.IsDescending())
		{
			sort(nums.begin(), nums.end(), std::greater<int>());
		}
		else
		{
			sort(nums.begin(), nums.end()); //Ascending sorting, uses for Ascending and Random load
		}

		for (int n = 0; n < elems; n++)
		{
			Range elem;
			elem.LowAddress = (TADDR)nums[n*2];
			elem.HighAddress = (TADDR)nums[n*2+1];
			
			if (load_settings.IsRandom()) //random load is building on Ascending sorting
			{
				int index = rand()%(ranges.size() + 1);			
				vector<Range>::iterator it = (ranges.begin() + index);
				ranges.insert(it, elem);
			}
			else
			{
				ranges.push_back(elem);
			}
		}

		for (int j = 0; j < elems; ++j)
		{
			PRINTF("j=%d\n", j);
			TADDR LowAddress = (TADDR)ranges[j].LowAddress;
			TADDR HighAddress = (TADDR)ranges[j].HighAddress;
			PRINTF("adding low=%08x, high=%08x\n", LowAddress, HighAddress);

			EM->AddCodeRange(LowAddress,
                                    HighAddress,
                                    nullptr, //pJit
                                    RangeSection::RANGE_SECTION_CODEHEAP,
                                    nullptr);//pHp

			if ((rand()%load_settings.GetDeleteDivisor() == 0))
			{
				//EM->DumpReaderArray();

				PRINTF("deleting range, j == %d, low = %08x\n", j, LowAddress);
				fflush(stdout);
				EM->DeleteRange(LowAddress);
				//EM->DumpReaderArray();
//TODO make advanced Delete-load
				deleted_ranges.push_back(j);
			}

			PRINTF("5\n");
			for (int k = 0; k <= j; ++k)
			{
				bool skip = false;
				for(auto in: deleted_ranges)
				{
					if (k == in)
					{
						skip = true;
						PRINTF("skipping k == %d\n", k);
						fflush(stdout);
						break;
					}
				}
				if (skip)
					continue;


				for (int i = 0; i < load_settings.GetReadLoadDensity(); i++)
				{
//TODO false positive tests
					int random_delta = rand()%((unsigned long long)ranges[k].HighAddress - (unsigned long long)ranges[k].LowAddress - 1);
					//int noise = rand()%100 - 50;
					int noise = 0;
					TADDR pCode = (TADDR)((unsigned long long)ranges[k].LowAddress + random_delta + noise);

					RangeSection *pRS = EM->GetRangeSection(pCode);
					if (!pRS) 
					{
						printf("element not found, i = %d, j = %d, k = %d, elem = %08x, search for %08x\n", i, j, k, ranges[k].LowAddress, pCode);
						//for (int n = 0; n < RangeSectionSize; ++n)
						//{
						//	printf("%08x:%08x ", pRangeSectionHandleArray[n].LowAddress, pRangeSectionHandleArray[n].pRS->HighAddress);
						//}
						//printf("RangeSectionSize=%d", RangeSectionSize);
						//printf("\n\n");
						//EM->DumpReaderArray();
						fflush(stdout);
						return -1;
					}
					if(ranges[k].LowAddress != pRS->LowAddress)
					{
						TADDR rs_low = pRS->LowAddress;  
						TADDR rs_high = pRS->HighAddress;  
						TADDR m_low = ranges[k].LowAddress;
						TADDR m_high = ranges[k].HighAddress;
						printf("section broken: i = %d, j = %d, k = %d, elem = %08x, search for %08x\n", i, j, k, m_low, pCode, rs_low);
						printf("section broken: pRS->LowAddress=%08x:%08x, ranges[%d].LowAddress=%08x:%08x\n",((TADDR)rs_low)>>32, rs_low , k, ((TADDR)m_low)>>32, m_low);
						printf("section broken: pRS->HighAddress=%08x:%08x, ranges[%d].HighAddress=%08x:%08x\n",((TADDR)rs_high)>>32, rs_high, k, ((TADDR)m_high)>>32, m_high);
						fflush(stdout);
						debug_trap();
						return -1;
					}
				}
			}

		}
	}
	

	//PRINTF("RAND_MAX = %08x\n" ,RAND_MAX);
	printf("test succeeded\n");
	return 0;
}
