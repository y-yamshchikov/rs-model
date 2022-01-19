
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <sched.h>
#include "load.h"
//#include <time.h>
#include <vector>
//#define PRINTF printf
#define PRINTF(...) 
#include "codeman.h"

using namespace std;

void debug_trap(void)
{
	printf("DEBUG TRAP\n");
	fflush(stdout);
	while(true)
		sleep(1);
}

LoadSettings::LoadSettings()
{
	seed_is_set = false;
	SetReadLoadDensity(10000);
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

void adder(ExecutionManager *EM, const LoadSettings &load_settings, const std::vector<Range> &ranges, int i, int batch_no);
void reader(ExecutionManager *EM, const LoadSettings &load_settings, const std::vector<Range> &ranges, int i, int batch_no);

int main(int argc, char* argv[])
{
	if (argc > 1)
	{
		load_multithreaded_rw_async(true, atoi(argv[1]));
		//load_multithreaded(true, atoi(argv[1]));
		//load_singlethreaded(true, atoi(argv[1]));
	}
	else
	{
		load_multithreaded_rw_async(false, 0);
		//load_multithreaded(false, 0);
		//load_singlethreaded(false, 0);
	}

	return 0;
}

int load_singlethreaded(bool set_seed, unsigned int seed)
{

	const int epochs = 1000;	
	const int elems = 1000;	

	LoadSettings load_settings;
	load_settings.OrderRandom();


	if (set_seed)
	{
		load_settings.SetSeed(seed);
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


				for (int m = 0; m < load_settings.GetReadLoadDensity(); m++)
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


int load_multithreaded(bool set_seed, unsigned int seed)
{
	const int epochs = 1000;	
	const int elems = 1000;	

	LoadSettings load_settings;
	load_settings.OrderRandom();


	if (set_seed)
	{
		load_settings.SetSeed(seed);
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

		vector<Range> ranges_part[4];	

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

				int part = rand()%4;
				int part_index = rand()%(ranges_part[part].size() + 1);			
				vector<Range>::iterator it_part = (ranges_part[part].begin() + part_index);
				ranges_part[part].insert(it_part, elem);
			}
			else
			{
				ranges.push_back(elem);
				ranges_part[n%4].push_back(elem);
			}
		}
//general array created
//partial arrays created

		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(0, &cpu_set);
		std::thread adder_1(adder,EM, load_settings, ranges_part[0], i, 0);
		int err = pthread_setaffinity_np(adder_1.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(2, &cpu_set);
		std::thread adder_2(adder,EM, load_settings, ranges_part[1], i, 1);
		err = pthread_setaffinity_np(adder_2.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(4, &cpu_set);
		std::thread adder_3(adder,EM, load_settings, ranges_part[2], i, 2);
		err = pthread_setaffinity_np(adder_3.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(6, &cpu_set);
		std::thread adder_4(adder,EM, load_settings, ranges_part[3], i, 3);
		err = pthread_setaffinity_np(adder_4.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		adder_1.join();
		adder_2.join();
		adder_3.join();
		adder_4.join();

//now read asynchronously:
		CPU_ZERO(&cpu_set);
		CPU_SET(0, &cpu_set);
		std::thread reader_1(reader,EM, load_settings, ranges_part[0], i, 0);
		err = pthread_setaffinity_np(reader_1.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(2, &cpu_set);
		std::thread reader_2(reader,EM, load_settings, ranges_part[1], i, 1);
		err = pthread_setaffinity_np(reader_2.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(4, &cpu_set);
		std::thread reader_3(reader,EM, load_settings, ranges_part[2], i, 2);
		err = pthread_setaffinity_np(reader_3.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		CPU_ZERO(&cpu_set);
		CPU_SET(6, &cpu_set);
		std::thread reader_4(reader,EM, load_settings, ranges_part[3], i, 3);
		err = pthread_setaffinity_np(reader_4.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		reader_1.join();
		reader_2.join();
		reader_3.join();
		reader_4.join();
	}
	
	

	//PRINTF("RAND_MAX = %08x\n" ,RAND_MAX);
	printf("test succeeded\n");
	return 0;
}


void adder(ExecutionManager *EM, const LoadSettings &load_settings, const std::vector<Range> &ranges, int i, int batch_no)
{
	printf("adder started: batch_no = %d\n", batch_no);
	fflush(stdout);
	int elems = ranges.size();

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
		//printf("adder with batch_no = %d, j = %d\n", batch_no, j);
	}
	printf("adder ended: batch_no = %d\n", batch_no);
	fflush(stdout);
}

void reader(ExecutionManager *EM, const LoadSettings &load_settings, const std::vector<Range> &ranges, int i, int batch_no)
{
	int elems = ranges.size();

	for (int k = 0; k < elems; ++k)
	{

		for (int m = 0; m < load_settings.GetReadLoadDensity(); m++)
		{
//TODO false positive tests
			int random_delta = rand()%((unsigned long long)ranges[k].HighAddress - (unsigned long long)ranges[k].LowAddress - 1);
			//int noise = rand()%100 - 50;
			int noise = 0;
			TADDR pCode = (TADDR)((unsigned long long)ranges[k].LowAddress + random_delta + noise);

			RangeSection *pRS = EM->GetRangeSection(pCode);
			if (!pRS) 
			{
				debug_trap();
				printf("element not found, i = %d, batch_no = %d, elems = %d, k = %d, elem = %08x, search for %08x\n", i, batch_no,  elems, k, ranges[k].LowAddress, pCode);
				//for (int n = 0; n < RangeSectionSize; ++n)
				//{
				//	printf("%08x:%08x ", pRangeSectionHandleArray[n].LowAddress, pRangeSectionHandleArray[n].pRS->HighAddress);
				//}
				//printf("RangeSectionSize=%d", RangeSectionSize);
				//printf("\n\n");
				//EM->DumpReaderArray();
				fflush(stdout);
				debug_trap();
				exit(-1);
			}
			if(ranges[k].LowAddress != pRS->LowAddress)
			{
				TADDR rs_low = pRS->LowAddress;  
				TADDR rs_high = pRS->HighAddress;  
				TADDR m_low = ranges[k].LowAddress;
				TADDR m_high = ranges[k].HighAddress;
				printf("section broken: i = %d, batch_no, elems = %d, k = %d, elem = %08x, search for %08x\n", i, batch_no, elems, k, m_low, pCode, rs_low);
				printf("section broken: pRS->LowAddress=%08x:%08x, ranges[%d].LowAddress=%08x:%08x\n",((TADDR)rs_low)>>32, rs_low , k, ((TADDR)m_low)>>32, m_low);
				printf("section broken: pRS->HighAddress=%08x:%08x, ranges[%d].HighAddress=%08x:%08x\n",((TADDR)rs_high)>>32, rs_high, k, ((TADDR)m_high)>>32, m_high);
				fflush(stdout);
				debug_trap();
				exit(1);
			}
		}
	}
}



int load_multithreaded_rw_async(bool set_seed, unsigned int seed)
{
	const int epochs = 1000;	
	const int elems = 1000;	

	LoadSettings load_settings;
	load_settings.OrderRandom();


	if (set_seed)
	{
		load_settings.SetSeed(seed);
	}
	printf("seed = %d\n", load_settings.GetSeed());
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

		vector<Range> ranges_part[4];	

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

				int part = rand()%4;
				int part_index = rand()%(ranges_part[part].size() + 1);			
				vector<Range>::iterator it_part = (ranges_part[part].begin() + part_index);
				ranges_part[part].insert(it_part, elem);
			}
			else
			{
				ranges.push_back(elem);
				ranges_part[n%4].push_back(elem);
			}
		}
		printf("ranges_part[0].size = %d\n", ranges_part[0].size());
//general array created
//partial arrays created
		fprintf(stderr, "i=%d\n", i);

		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(0, &cpu_set);
		std::thread adder_1(adder,EM, load_settings, ranges_part[0], i, 0);
		int err = pthread_setaffinity_np(adder_1.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		adder_1.join();

		CPU_ZERO(&cpu_set);
		CPU_SET(0, &cpu_set);
		std::thread reader_1(reader,EM, load_settings, ranges_part[0], i, 0);
		err = pthread_setaffinity_np(reader_1.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}
//-----------------------

		CPU_ZERO(&cpu_set);
		CPU_SET(2, &cpu_set);
		std::thread adder_2(adder,EM, load_settings, ranges_part[1], i, 1);
		err = pthread_setaffinity_np(adder_2.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		adder_2.join();
/*

		CPU_ZERO(&cpu_set);
		CPU_SET(2, &cpu_set);
		std::thread reader_2(reader,EM, load_settings, ranges_part[1], i, 1);
		err = pthread_setaffinity_np(reader_2.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}
//-----------------------

		CPU_ZERO(&cpu_set);
		CPU_SET(4, &cpu_set);
		std::thread adder_3(adder,EM, load_settings, ranges_part[2], i, 2);
		err = pthread_setaffinity_np(adder_3.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		adder_3.join();

		CPU_ZERO(&cpu_set);
		CPU_SET(4, &cpu_set);
		std::thread reader_3(reader,EM, load_settings, ranges_part[2], i, 2);
		err = pthread_setaffinity_np(reader_3.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}
//-----------------------

		CPU_ZERO(&cpu_set);
		CPU_SET(6, &cpu_set);
		std::thread adder_4(adder,EM, load_settings, ranges_part[3], i, 3);
		err = pthread_setaffinity_np(adder_4.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

		adder_4.join();

		CPU_ZERO(&cpu_set);
		CPU_SET(6, &cpu_set);
		std::thread reader_4(reader,EM, load_settings, ranges_part[3], i, 3);
		err = pthread_setaffinity_np(reader_4.native_handle(), sizeof(cpu_set_t), &cpu_set);
		if (err != 0)
		{
			printf("pthread_setaffinity_np failed\n");
			exit(1);
		}

*/
		reader_1.join();
//		reader_2.join();
//		reader_3.join();
//		reader_4.join();
	}

	//PRINTF("RAND_MAX = %08x\n" ,RAND_MAX);
	printf("test succeeded\n");
	return 0;
}
