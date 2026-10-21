//Project 2: cache and memory performance profiling
//libraries
#include <vector>
#include <iostream>
//#include <windows.h>
#include <algorithm> //from bing copilot "std::generate in c++ (for gcc)"
//copilot says to put these 3 lines down for thread pinning in linux
//#define _GNU_SOURCE
#include <sched.h>//linux scheduler
#include <pthread.h>//thread stuff

//From copilot, I did these for using perf_event headers in c++,
//prompt "I plan to keep it simple and effective. I was told by a friend that instead of using perf in command line, that I should use perf_events headers in c++ instead."
//prompt "What libraries do I need for this first? (what do I need to #include?)"
//required headers for perf_event_open
#include <linux/perf_event.h> //perf_event_attr, event types
#include <linux/hw_breakpoint.h> //sometimes needed for HW events
#include <sys/syscall.h> //syscall()
#include <sys/ioctl.h> //ioctl()
#include <unistd.h> //read(), close()
#include <string.h> //memset()
#include <stdint.h> //uint64_t
#include <stdlib.h> //exit(), malloc()
#include <stdio.h> //printf()

#include <random>//recommended by gemini for random generation

#include <sys/mman.h> //required for mmap

#include <fstream> //for writing output to .csv files

//Helper Functions
//making vector of randomized long integers, modified version of code written by gemini, prompt "how to produce vector of random long in c++"
std::vector<long> GenerateRandomVector(long num){
      //generates vector with length num
      std::vector<long> results(num);
      //Initialize random number engine
      std::random_device rd; //seed generator
      std::mt19937_64 gen(rd()); //64-bit Mersenne Twister engine (perfect for long/64-bit integers)
      //define the distribution range
      std::uniform_int_distribution<long> distrib(std::numeric_limits<long>::min(), std::numeric_limits<long>::max());
      //fill the vector using std::generate and a lambda function
      std::generate(results.begin(), results.end(), [&]() {return distrib(gen);});
      //return the randomized vector
      return results;
}

//This benchmarking call harness was provided and written by copilot
static long perf_event_open(struct perf_event_attr *hw_event,
                            pid_t pid, int cpu, int group_fd,
                            unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
}

struct PerfCounter {
    int fd;
    uint64_t value;

    void setup(uint32_t type, uint64_t config) {
        struct perf_event_attr pe;
        memset(&pe, 0, sizeof(pe));
        pe.type = type;
        pe.size = sizeof(pe);
        pe.config = config;
        pe.disabled = 1;
        pe.exclude_kernel = 1;
        pe.exclude_hv = 1;
        fd = perf_event_open(&pe, 0, -1, -1, 0);
    }

    void start() {
        ioctl(fd, PERF_EVENT_IOC_RESET, 0);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    }

    void stop() {
        ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
        read(fd, &value, sizeof(uint64_t));
        //this exit part below makes sure that it doesn't leak fd file headers
        //this is fine as long as you never need to reuse the same PerfCounter object after stop(),
        ::close(fd);
        fd = -1;
    }
};


//Experimental knobs:
//-access pattern: sequential vs random: strides near 64B,256B,1024B
//-read/write ratio: 100% read, 100% write, 70/30, and 50/50 where supported
//-access intensity: outstanding requests or threads, including loaded-latency sweeps
//-working set size: range from L1-resident to DRAM-resident
//-page locality: 4KB pages vs. huge pages where available

//Required Experiments and Plots:
//-Zero-queue baselines: per-level latency table with units and CPU-frequency conversion where useful
//-pattern and granularity sweep: latency and bandwidth across sequential/random and stride/granularity settings
//-read/write mix sweep: compare throughput and latency under the required mixes
//-intensity sweep: plot throughput vs. latency, identify the knee, and relate it to little's law
//-working-set sweep: annotate L1/L2/LLC/DRAM transtion regions
//-cache-miss impact: vary footprint or access pattern and correlate performance counters with runtime
//-TLB-miss impact: vary page locality and evaluate huge pages where possible
//-show one benchmark pitfall, such as accidentally measuring cache reuse instead of DRAM, failing to pin threads, or allowing frequency scaling to distort results

//Z[i] = X[i] * Y[i] (66% read, 33% write)
//-->Intel MLC is replacing the purpose of this kernel
void ElementWiseMultiply(std::vector<long>& x, std::vector<long>& y, std::vector<long>& z, size_t strides, size_t alignmentOffset){
    //pointer version, allows for lower overhead and custom strides
    size_t xsize = x.size();
    size_t ysize = y.size();
    size_t zsize = z.size();
    long* __restrict xpointer = x.data() + alignmentOffset;//Prologue?
    long* __restrict ypointer = y.data() + alignmentOffset;//Prologue?
    long* __restrict zpointer = z.data() + alignmentOffset;//Prologue?
    if((xsize == ysize) && (ysize == zsize) && (alignmentOffset < zsize)){
        for(size_t i = 0; i < zsize - alignmentOffset; i += strides){//Main Vector Loop?
            zpointer[i] = xpointer[i] * ypointer[i];
        }
    }//No Epilogue/tail handling?
    /**
    if((x.size() == y.size()) && (y.size() == z.size())){
        for(int i = 0; i < z.size(); i++){
            z[i] = x[i] * y[i];
        }
    }
    **/
}

//good for #5 (working set sweep), #6 (Cache-miss impact), and maybe #8 (show a benchmarking pitfall)
void AXPY(long a, std::vector<long>& x, std::vector<long>& y, size_t strides, size_t alignmentOffset){//AXPY
    //pointer version, allows for lower overhead and custom strides
    size_t xsize = x.size();
    size_t ysize = y.size();
    long* __restrict xpointer = x.data() + alignmentOffset;//Prologue?
    long* __restrict ypointer = y.data() + alignmentOffset;//Prologue?
    if((xsize == ysize) && (alignmentOffset < ysize)){
        for(size_t i = 0; i < ysize - alignmentOffset; i += strides){//Main Vector Loop?
            ypointer[i] += a * xpointer[i];
        }
    }//No Epilogue/tail handling?
    /**
    if(x.size() == y.size()){
        for(int i = 0; i < y.size(); i++){
            y[i] += a * x[i];
        }
    }
    **/
}

//--> good for #7 (TLB-Miss impact)
long DotProductReduction(std::vector<long>& x, std::vector<long>& y, size_t strides, size_t alignmentOffset){
    //pointer version, allows for lower overhead and custom strides
    size_t xsize = x.size();
    size_t ysize = y.size();
    long* __restrict xpointer = x.data() + alignmentOffset;//Prologue?
    long* __restrict ypointer = y.data() + alignmentOffset;//Prologue?
    if((xsize == ysize) && (alignmentOffset < ysize)){
        long s = 0;
        for(size_t i = 0; i < ysize - alignmentOffset; i += strides){//Main Vector Loop?
            s += ypointer[i] * xpointer[i];
        }
        return s;//Epilogue/tail handling?
    }
    return 0;//Epilogue/tail handling?
    /**
    if(x.size() == y.size()){
        s = 0.0f;
        for(int i = 0; i < x.size(); i++){
            s += x[i] * y[i];
        }
    }
    **/
}

//Benchmarking functions


/**
void BenchmarkWorkingSetSweep(std::vector<long>& workingSet){
      //I was thinking something along these lines:
      //0. for each workingsetsize in workingSet:
      //--> 1. prepare randomized data of size workingsetsize
      //--> 2. benchmark
      //--> 3. repeat steps 1 and 2, for 20 runs, and average results
      //4. write results to output txt file
}

void BenchmarkCacheMissImpact(long mxsize, std::vector<size_t>& strides){
      //I was thinking something along these lines:
      //0. for each stride in strides:
      //--> 1. prepare randomized data mxsize
      //--> 2. benchmark
      //--> 3. repeat steps 1 and 2, for 20 runs, and average results
      //4. write results to output txt file
}

void BenchmarkTLBImpact(std::vector<size_t>& pageSizes, std::vector<size_t>& pageStrides){
      //I wasn't quite sure about this one
      //0. Prepare 4KiB page file data
      //--> 1. Stride sweep this? maybe just a good stride and bad stride?
      //0. Prepare large page file data (2MiB?)
      //--> 1. Stride sweep this? maybe just a good stride and a bad stride?
      //Idk about this one yet

}
**/

//These are the benchmarking functions written by copilot. Prompt "Here is my code so far" (context, "writing benchmarking harnesses using perf_event headers in c++")
//modified by me, for testing methodology reasons
void BenchmarkWorkingSetSweep(std::vector<long>& workingSet) {
    //I asked copilot to write output to .csv files
    std::ofstream out("WorkingSetSweep.csv");
    if(!out.is_open()){
        std::cerr << "Error opening WorkingSetSweep.csv\n";
        exit(1);
    }
    //
    const int trials = 20;

    out << "Working_Set_Size_Bytes,AVG_CPU_Cycles,AVG_CPU_Instructions,AVG_L1-D_Hitrate_Percentage,AVG_LLC_Hitrate_Percentage\n";

    for (long N : workingSet) {
        std::vector<long> x(N), y(N);
        //for (long i = 0; i < N; i++) {
        //    x[i] = i;
        //    y[i] = i * 2;
        //}
        x = GenerateRandomVector(N);
        y = GenerateRandomVector(N);

        uint64_t totalCycles = 0;
        uint64_t totalInstr   = 0;
        double l1hitrate = 0.0;
        double l3hitrate = 0.0;

        for (int t = 0; t < trials; t++) {

            // Warmup (not measured)
            for (long i = 0; i < N; i += 64)//touches every cache line of 64bytes
                y[i] += x[i];

            PerfCounter cycles, instr, l1dmiss, l3miss;//optionally llc references
            PerfCounter l1accesses, l3accesses;//these are how many read accesses per cache, i want hitrate
            cycles.setup(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
            instr.setup(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
            l1dmiss.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
            l3miss.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
            l1accesses.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));
            l3accesses.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));


            cycles.start();
            instr.start();
            l1dmiss.start();
            l3miss.start();
            l1accesses.start();
            l3accesses.start();

            AXPY(3, x, y, 1, 0);

            cycles.stop();
            instr.stop();
            l1dmiss.stop();
            l3miss.stop();
            l1accesses.stop();
            l3accesses.stop();

            totalCycles += cycles.value;
            totalInstr  += instr.value;
            l1hitrate += 1.0 - (l1dmiss.value / (double)l1accesses.value);
            l3hitrate += 1.0 - (l3miss.value / (double)l3accesses.value);

        }

        out << N * sizeof(long) << ","
                  << (totalCycles / trials) << ","
                  << (totalInstr  / trials) << ","
                  << 100.0 * (l1hitrate / (double)trials) << ","
                  << 100.0 * (l3hitrate / (double)trials)
                  << "\n";
    }
    out.close();
    if(!out){
        std::cerr << "WorkingSetSweep.csv: Write failed\n";
    }
}


void BenchmarkCacheMissImpact(long N, std::vector<size_t>& strides) {
    //I asked copilot to write output to .csv files
    std::ofstream out("CacheMissImpact.csv");
    if(!out.is_open()){
        std::cerr << "Error opening CacheMissImpact.csv\n";
        exit(1);
    }
    //
    const int trials = 20;

    std::vector<long> x(N), y(N);
    //for (long i = 0; i < N; i++) {
    //    x[i] = i;
    //    y[i] = i * 2;
    //}
    x = GenerateRandomVector(N);
    y = GenerateRandomVector(N);

    out << "Stride_Bytes,AVG_CPU_Cycles,AVG_L1-D_Hitrate_Percentage,AVG_LLC_Hitrate_Percentage,AVG_L1-D_Miss_Count,AVG_LLC_Miss_Count\n";

    for (size_t stride : strides) {
        if (stride == 0) continue;

        uint64_t totalCycles = 0;
        uint64_t totalL1     = 0;
        uint64_t totalLLC    = 0;
        double l1hitrate = 0.0;
        double llchitrate = 0.0;

        for (int t = 0; t < trials; t++) {

            PerfCounter cycles, l1, llc;//optionally instructions,//we should get l1 loads and llc loads so we can get the cache hit rate
            PerfCounter l1accesses, llcaccesses;//this is how many accesses per cache layer
            cycles.setup(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);

            l1.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_L1D |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));

            llc.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_LL |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));

            l1accesses.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_L1D |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));

            llcaccesses.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_LL |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));

            cycles.start();
            l1.start();
            llc.start();
            l1accesses.start();
            llcaccesses.start();

            AXPY(3, x, y, stride, 0);

            cycles.stop();
            l1.stop();
            llc.stop();
            l1accesses.stop();
            llcaccesses.stop();

            totalCycles += cycles.value;
            totalL1     += l1.value;
            totalLLC    += llc.value;
            l1hitrate += 1.0 - (l1.value / (double)l1accesses.value);
            llchitrate += 1.0 - (llc.value / (double)llcaccesses.value);
        }

        out << stride * 8 << ","
                  << (totalCycles / trials) << ","
                  << 100.0 * (l1hitrate / (double)trials) << ","
                  << 100.0 * (llchitrate / (double)trials) << ","
                  << (totalL1     / trials) << ","
                  << (totalLLC    / trials)
                  << "\n";
    }
    out.close();
    if(!out){
        std::cerr << "CacheMissImpact.csv: Write failed\n";
    }
}

long* AllocateHugePageArray(size_t numElements) {
    size_t bytes = numElements * sizeof(long);

    void* ptr = mmap(nullptr, bytes,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);

    if (ptr == MAP_FAILED) {
        ptr = mmap(nullptr, bytes,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
    }

    return reinterpret_cast<long*>(ptr);
}

void BenchmarkTLBImpact(std::vector<size_t>& pageSizes,
                        std::vector<size_t>& pageStrides)
{
    //I asked copilot to write output to .csv files
    std::ofstream out("TLBMissImpact.csv");
    if(!out.is_open()){
        std::cerr << "Error opening TLBMissImpact.csv\n";
        exit(1);
    }
    //
    const int trials = 20;
    long N = 8'388'608; // 64MB

    out << "PageSize_Bytes,Stride_Elements,AVG_CPU_Cycles,AVG_DataTLB_Miss_Count,AVG_DataTLB_HitRate_Percentage\n";

    // --- Small pages (std::vector)
    std::vector<long> x(N), y(N);
    //for (long i = 0; i < N; i++) {
    //    x[i] = i;
    //    y[i] = i * 2;
    //}
    x = GenerateRandomVector(N);
    y = GenerateRandomVector(N);

    for (size_t strideElems : pageStrides) {
        uint64_t totalCycles = 0;
        uint64_t totalTLB    = 0;
        double tlbhitrate = 0.0;

        for (int t = 0; t < trials; t++) {
            PerfCounter cycles, tlb;//dtlb load misses, dtlb loads, optionally instructions
            PerfCounter tlbaccesses;//dtlb read accessses
            cycles.setup(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);

            tlb.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_DTLB |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));

            tlbaccesses.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));

            cycles.start();
            tlb.start();
            tlbaccesses.start();

            long s = DotProductReduction(x, y, strideElems, 0);

            cycles.stop();
            tlb.stop();
            tlbaccesses.stop();

            totalCycles += cycles.value;
            totalTLB    += tlb.value;
            tlbhitrate += 1.0 - (tlb.value / (double)tlbaccesses.value);
        }

        out << pageSizes[0] * 8 << "," << strideElems << ","
                  << (totalCycles / trials) << ","
                  << (totalTLB    / trials) << ","
                  << 100.0 * (tlbhitrate / (double)trials)
                  << "\n";
    }

    // --- Huge pages
    long* xHuge = AllocateHugePageArray(N);
    long* yHuge = AllocateHugePageArray(N);
    //for (long i = 0; i < N; i++) {
    //    xHuge[i] = i;
    //    yHuge[i] = i * 2;
    //}
    //I asked copilot again "how can I randomize the huge pages?", it gave this
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<long> distrib(std::numeric_limits<long>::min(), std::numeric_limits<long>::max());

    for(long i = 0; i < N; i++){
        xHuge[i] = distrib(gen);
        yHuge[i] = distrib(gen);
    }


    for (size_t strideElems : pageStrides) {
        uint64_t totalCycles = 0;
        uint64_t totalTLB    = 0;
        double tlbhitrate = 0.0;

        for (int t = 0; t < trials; t++) {
            PerfCounter cycles, tlb, tlbaccesses;
            cycles.setup(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);

            tlb.setup(PERF_TYPE_HW_CACHE,
                PERF_COUNT_HW_CACHE_DTLB |
                (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS << 16));
            
            tlbaccesses.setup(PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_ACCESS << 16));

            cycles.start();
            tlb.start();
            tlbaccesses.start();

            long s = 0;
            for (size_t i = 0; i < N; i += strideElems)
                s += xHuge[i] * yHuge[i];

            cycles.stop();
            tlb.stop();
            tlbaccesses.stop();

            totalCycles += cycles.value;
            totalTLB    += tlb.value;
            tlbhitrate += 1.0 - (tlb.value / (double)tlbaccesses.value);
        }

        out << pageSizes[1] * 8 << "," << strideElems << ","
                  << (totalCycles / trials) << ","
                  << (totalTLB    / trials) << ","
                  << 100.0 * (tlbhitrate / (double)trials)
                  << "\n";
    }
    out.close();
    if(!out){
        std::cerr << "TLBMissImpact.csv: Write failed\n";
    }
}

//main is a work in progress
int main(){
    //setup/knobs
    //Access Pattern: Sequential vs Random (Intel MLC)
    //Access Pattern: strides near 64B, 256B, 1024B (Kernel)
    //Read/write ratio: 100% read, 100% write, 70/30, and 50/50 where supported (multiple kernels?)
    //Access intensity: outstanding requests or threads, including loaded-latency sweeps (probably not kernels?)
    //Working-set size: range from L1-resident to DRAM-resident (Kernel and MLC)
    //Page locality: 4KB pages vs. huge pages where available (MLC?)

    //working set sweep
    std::vector<long> workingSetSizes = {//for FP64 only
        2'048, //~16KB (L1 I, per core)
        4'608, //~36KB (L1 D, per core)
        131'072, //~1MB (L2, per core)
        786'432, //~6MB (L3 shared)
        8'388'608 //~64MB (DRAM)
    };

    std::vector<size_t> strides = {
        0, //no stride
        8, //64Bytes (8 element stride of 64-bit elements)
        32, //256Bytes (32 element stride of 64-bit elements)
        128 //1024Bytes (128 element stride of 64-bit elements)
    };

    std::vector<size_t> pageSizes = {
      512, //~4KiB (standard page size)
      262'144 //~2MiB (extremelly large page size)
    };

    std::vector<size_t> pageStrides = {
      1, //good for both 4KiB pages and 2MiB pages
      512, //bad for 4KiB pages (slow, many TLB misses)
      262144 //bad locality, still fast, low TLB misses (for 2MiB pages)
    };

    //Note: this group of code below is for thread pinning. From Copilot, it pins to thread0, "first off, we should do thread pinning"
    /**
    //pin to core/thread 0 (windows only)
    HANDLE hThread = GetCurrentThread();
    //1 = use CPU 0 only (bitmask)
    DWORD_PTR mask = 1;
    SetThreadAffinityMask(hThread, mask);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    **/
    //this chunk is from copilot "I changed the platform to linux debian, can you help me adjust it for this new platform? (the old one only works on windows)"
    //"I also want to keep the frequency constant, disable turbo boost, and disable Intel speedstep"
    //new core/thread pinning for linux
    //core isolation requires cset and cpuset
    //sudo apt install cpuset <-- this should just install the cset with it
    //first, requires you to run the program like this: 
    //sudo cset shield --cpu 0 --kthread=on
    //sudo cset shield --exec -- ./your_program
    //then, to disable that shield: sudo cset shield --reset OR sudo cset shield -r
    int core_id = 0;//pins to core 0 by default
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    //Deliverables #5-8 here:

    //5.) Working-set sweep: annotate L1/L2/LLC/DRAM transition regions.
    //-->AXPY
    BenchmarkWorkingSetSweep(workingSetSizes);
    //6.) Cache-miss impact: vary footprint or access pattern and correlate perf counters with runtime.
    //-->AXPY
    BenchmarkCacheMissImpact(8'388'608, strides);//big DRAM sized allocations
    //7.) TLB-miss impact: vary page locality and evaluate huge pages where possible.
    //-->DotProductReduction
    BenchmarkTLBImpact(pageSizes, pageStrides);
    //8.) Show one benchmark pitfall, such as accidentally measuring cache reuse instead of DRAM, failing to pin threads, or allowing frequency scaling to distort results.
    //-->AXPY

    //end here
    return 0;
    //NOTE: how to compile and run properly: gcc project2cache.cpp -lstdc++ -o project2cache.exe
    //that -lstdc++ is what causes the linker for the libraries to work
    //NOTE: you must also run this with sudo or else the perf benchmark counters won't work
}
/**
 * NOTES ON intel MLC:
 * how to use properly:
 * - Open powershell as admin
 * - go to the mlc folder windows folder
 * 
 * To run intel mlc:
 * - type: .\mlc.exe -e0 (sequential access by default) (you can also add more flags after this)
 * 
 * There are some more flags:
 * - -r: random access
 * - 
 * 
 * Here are some search prompts for more information:
 * - "intel mlc how to measure l1 and l3 cache latency"
 * - (gives some compiler flags) link: www.intel.com/content/www/us/en/developer/articles/tool/intelr-memory-latency-checker.html
 */

/*Task to command line inputs

1.) Zero-queue latency (idle latency): 
2.) Pattern/granularity sweep (Sequential/random + block size variation): 
3.) Read/write mix sweep (built-in R/W ratio bandwidth tests.): 
4.) Intensity sweep (knee)(loaded latency + bandwidth points.): 
*/

/*
mlc --latency_matrix

      prints a matrix of local and cross-socket memory latencies

mlc --bandwidth_matrix

      prints a matrix of local and cross-socket memory b/w

mlc --peak_injection_bandwidth

      prints peak memory b/w (core generates requests at fastest possible rate) for various read-write ratios with all local accesses

mlc --max_bandwidth

      prints maximum memory b/w (by automatically varying load injection rates) for various read-write ratios with all local accesses

mlc --idle_latency

      prints the idle memory latency of the platform

mlc --loaded_latency

      prints the loaded memory latency of the platform

mlc --c2c_latency

      prints the cache-to-cache transfer latencies of the platform

mlc -e

      do not modify prefetcher settings

mlc --memory_bandwidth_scan 

      prints memory bandwidth across entire memory for each 1 GB address range

There are more options for each of the commands above. Those are documented in the readme file in more detail and can be downloaded*/

//NOTE: FOR Project 3, use FIO instead of programming (cooper said this)