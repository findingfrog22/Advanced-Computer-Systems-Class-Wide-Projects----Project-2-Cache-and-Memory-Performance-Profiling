//Project 2: cache and memory performance profiling
//libraries
#include <vector>
#include <iostream>
#include <windows.h>

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
    std::vector<long> workingSetSizes = {
        4'096, //~32KB (L0 D)
        6'144, //~56KB (L0 I)
        16'384, //~128KB (L1)
        262'144, //~2MB (L2, Pcore)
        786'432, //~6MB (L3 shared)
        8'388'608 //~64MB (DRAM)
    };
    //32KB: L0 D
    //56KB: L0 I
    //128KB: L1 (P core)
    //2MB: L2 (P core)
    //6MB: L3 (shared, P cores)
    //64MB: DRAM

    std::vector<size_t> strides = {
        0, //no stride
        8, //64Bytes (8 element stride of 64-bit elements)
        32, //256Bytes (32 element stride of 64-bit elements)
        128 //1024Bytes (128 element stride of 64-bit elements)
    };

    //Note: this group of code below is for thread pinning. From Copilot, it pins to thread0, "first off, we should do thread pinning"
    //pin to core/thread 0 (windows only)
    HANDLE hThread = GetCurrentThread();
    //1 = use CPU 0 only (bitmask)
    DWORD_PTR mask = 1;
    SetThreadAffinityMask(hThread, mask);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    //
    return 1;
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