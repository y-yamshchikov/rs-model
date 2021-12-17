
#include <cstring>
#include <cstdio>
#include <algorithm>
#include "load.h"
//#include <time.h>
#include <vector>
#define PRINTF printf
//#define PRINTF(...) 
#include "codeman.h"

using namespace std;

int main(int argc, char* argv[])
{
	const int epochs = 1000;	
	const int elems = 1000;	

	time_t t;
	unsigned int seed = (unsigned int) time(&t);
	if (argc > 1)
	{
		seed = atoi(argv[1]);
	}
	srand(seed);


	ExecutionManager *EM = new ExecutionManager();
	EM->Init();

	for (int i = 0; i < epochs; ++i)
	{
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
				for (auto x : nums)
				{
					if( rnd == x)
						f = true;
				} 
			}while ( f );
			nums.push_back(rnd);
		}

		sort(nums.begin(), nums.end());

		for (int n = 0; n < elems; n++)
		{
			Range elem;
			elem.LowAddress = (TADDR)nums[n*2];
			elem.HighAddress = (TADDR)nums[n*2+1];
			int index = rand()%(ranges.size() + 1);			
			vector<Range>::iterator it = (ranges.begin() + index);
			ranges.insert(it, elem);
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

			if ((rand()%10 == 0))
			{
				EM->DeleteRange(LowAddress);
//TODO make advanced Delete-load

				PRINTF("deleting range, j == %d, low = %08x\n", j, LowAddress);
				fflush(stdout);
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
					fflush(stdout);
					return -1;
				}
				if(ranges[k].LowAddress != pRS->LowAddress)
				{
					printf("section broken\n");
				}
				
				//if(ranges[k].HighAddress != pRS2->HighAddress)
				//{
				//	printf("section broken\n");
				//}
			}

		}
	}
	

	//PRINTF("RAND_MAX = %08x\n" ,RAND_MAX);
	printf("test succeeded\n");
	return 0;
}
