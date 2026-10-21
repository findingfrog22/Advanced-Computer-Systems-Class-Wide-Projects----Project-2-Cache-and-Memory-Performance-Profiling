#!/bin/bash

#btw, this only is meant for linux, don't try it on windows unless you know how to modify and configure this stuff

# "having bash script run itself with sudo priviledges"
# Check if the script is running as root; if not, re-execute itself with sudo
[ "$EUID" -ne 0 ] && exec sudo "$0" "$@"

#Environmental setup, for reproducibility:

#What I will vs will not do:
# - I had to make compromises to ensure that Intel MLC could work, while trying to be as consistent as I can
#What I will do:
#1.) Disable Turbo Boost --> Increases throughput+latency consistency
#2.) Lock the Frequency --> Increases throughput+latency consistency
#3.) Disable C states (besides C0, which will run 100% of the time) --> Reduces jitter and performance dips

#What I will not do: (mainly due to Intel MLC Quirks)
#1.) Core Isolation (especially to 1 core) --> Intel MLC likes to use all cores for latency and bandwidth
#2.) Disable SMT/Hyper Threading --> Intel MLC handles thread mapping with SMT/Hyperthreading, disabling it throws of HWID indexing

#set intel pstate to passive mode, allows for direct frequency change instead of preset governors
echo passive | tee /sys/devices/system/cpu/intel_pstate/status

#Setting governor to userspace (allows for direct frequency locking)
cpupower frequency-set -g userspace #this sets all cores to userspace mode, which allows for pinning exact frequency



#First, Disable Turbo Boost
echo 1 | tee /sys/devices/system/cpu/intel_pstate/no_turbo #this disables turbo boost on all cores

#Then, Disable C states
cpupower idle-set -D 0 #forces C0 active state by limiting it to 0 microseconds allowed in sleep state

#Finally, lock the frequency to 1.40GHz
cpupower frequency-set -f 1400MHz #this sets all cores to 1.4GHz (unit is in KHz by default)
# Clear explicit frequency constraints
cpupower frequency-set -d 1400MHz #sets lower frequency minimum
cpupower frequency-set -u 1400MHz #sets max frequency
cpupower frequency-info #this prints out the current frequency info for all cores, which should now be in userspace mode
#cpupower -c 0 monitor

#Now benchmark time

#set up for Intel MLC first

touch ZeroQueueBaselines.txt

touch SequentialStrideSweep.txt
touch SequentialGranularitySweep.txt

touch RandomStrideSweep.txt
touch RandomGranularitySweep.txt

touch ReadWriteMixSweep.txt
touch IntensitySweep.txt

#clears the txt files just in case

> ZeroQueueBaselines.txt
> SequentialStrideSweep.txt
> SequentialGranularitySweep.txt
> RandomStrideSweep.txt
> RandomGranularitySweep.txt
> ReadWriteMixSweep.txt
> IntensitySweep.txt

echo "Note: Cache capacities and buffer sizes are based on CPU model, so you may need to adjust it for yours (test is i5 1135g7)"
echo "Note: Cache/TLB hit rates and misses are based on read operations"
echo "Note: L2 cache will not be analyzed with perf counters due to its highly non-portable nature"

#Intel MLC Benchmarking (Parts 1-4):
#1.) Zero-queue baselines: per-level latency table with units and CPU-frequency conversion where useful.
echo "L1-I 16KB:\n" >> ZeroQueueBaselines.txt
sudo ./mlc --idle_latency -b16k | tee -a ZeroQueueBaselines.txt #L1-I per core. ~16KB
echo "\nL1-D 36KB:\n" >> ZeroQueueBaselines.txt
sudo ./mlc --idle_latency -b36k | tee -a ZeroQueueBaselines.txt #L1-D per core. ~36KB
echo "\nL2 1MB:\n" >> ZeroQueueBaselines.txt
sudo ./mlc --idle_latency -b1m | tee -a ZeroQueueBaselines.txt #L2 per core. ~1MB
echo "\nL3 6MB:\n" >> ZeroQueueBaselines.txt
sudo ./mlc --idle_latency -b6m | tee -a ZeroQueueBaselines.txt #L3 shared. ~6MB
echo "\nDRAM 64MB:\n" >> ZeroQueueBaselines.txt
sudo ./mlc --idle_latency -b64m | tee -a ZeroQueueBaselines.txt #DRAM. ~64MB

#2.) Pattern and granularity sweep: latency and bandwidth across sequential/random and stride/granularity settings.
#Sequential Stride Sweep
echo "64B stride, 128MiB buffer (Bandwidth + Latency):\n" >> SequentialStrideSweep.txt
sudo ./mlc --bandwidth_matrix -l64 -b128MiB | tee -a SequentialStrideSweep.txt #bandwidth, 64B stride (default), 128MiB buffer
sudo ./mlc --latency_matrix -l64 -b128MiB | tee -a SequentialStrideSweep.txt #latency, 64B stride (default), 128MiB buffer
echo "\n256B stride, 128MiB buffer (Bandwidth + Latency):\n" >> SequentialStrideSweep.txt
sudo ./mlc --bandwidth_matrix -l256 -b128MiB | tee -a SequentialStrideSweep.txt #bandwidth, 256B stride, 128MiB buffer
sudo ./mlc --latency_matrix -l256 -b128MiB | tee -a SequentialStrideSweep.txt #latency, 256B stride, 128MiB buffer
echo "\n1024B stride, 128MiB buffer (Bandwidth + Latency):\n" >> SequentialStrideSweep.txt
sudo ./mlc --bandwidth_matrix -l1024 -b128MiB | tee -a SequentialStrideSweep.txt #bandwidth, 1024B stride, 128MiB buffer
sudo ./mlc --latency_matrix -l1024 -b128MiB | tee -a SequentialStrideSweep.txt #latency, 1024B stride, 128MiB buffer

#Sequential Granularity Sweep (bandwidth doesn't support granularity settings)
echo "64B granularity (1 Cache Line), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D1 -b128MiB | tee -a SequentialGranularitySweep.txt #64B, standard for a single cache line
echo "\n4KiB granularity (Standard Page File Size, 64 Cache Lines), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D64 -b128MiB | tee -a SequentialGranularitySweep.txt #4096B (4KiB), standard for a page file
echo "\n128KiB granularity (Large page file, 2048 Cache Lines), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D2048 -b128MiB | tee -a SequentialGranularitySweep.txt #128KiB, large-ish page file
echo "\n2MiB granularity (Very Large page file, 32768 Cache Lines), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D32768 -b128MiB | tee -a SequentialGranularitySweep.txt #2MiB, massive page file
echo "\n4MiB granularity (Extremelly Large page file, 65536 Cache Lines), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D65536 -b128MiB | tee -a SequentialGranularitySweep.txt #4MiB, huge page file
echo "\n64MiB granularity (No caching, DRAM-only page file, 1048576 Cache Lines), 128MiB buffer (Latency Only):\n" >> SequentialGranularitySweep.txt
sudo ./mlc --latency_matrix -D1048576 -b128MiB | tee -a SequentialGranularitySweep.txt #64MiB, heavy DRAM test, no caching

#Random Granularity Sweep (bandwidth doesn't support granularity settings)
echo "64B granularity (1 Cache Line), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D1 -b128MiB | tee -a RandomGranularitySweep.txt #64B, standard for a single cache line
echo "\n4KiB granularity (Standard Page File Size, 64 Cache Lines), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D64 -b128MiB | tee -a RandomGranularitySweep.txt #4096B (4KiB), standard for a page file
echo "\n128KiB granularity (Large page file, 2048 Cache Lines), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D2048 -b128MiB | tee -a RandomGranularitySweep.txt #128KiB, large-ish page file
echo "\n2MiB granularity (Very Large page file, 32768 Cache Lines), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D32768 -b128MiB | tee -a RandomGranularitySweep.txt #2MiB, massive page file
echo "\n4MiB granularity (Extremelly Large page file, 65536 Cache Lines), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D65536 -b128MiB | tee -a RandomGranularitySweep.txt #4MiB, huge page file
echo "\n64MiB granularity (No caching, DRAM-only page file, 1048576 Cache Lines), 128MiB buffer (Latency Only):\n" >> RandomGranularitySweep.txt
sudo ./mlc --latency_matrix -r -D1048576 -b128MiB | tee -a RandomGranularitySweep.txt #64MiB, heavy DRAM test, no caching

#Random Stride Sweep (pointless?, no, we will do --latency_matrix with strides, but not --bandwidth_matrix becuase it doesn't support random access)
echo "64B stride, 128MiB buffer (Latency Only):\n" >> RandomStrideSweep.txt
sudo ./mlc --latency_matrix -r -l64 -b128MiB | tee -a RandomStrideSweep.txt #latency, 64B stride (default), 128MiB buffer
echo "\n256B stride, 128MiB buffer (Latency Only):\n" >> RandomStrideSweep.txt
sudo ./mlc --latency_matrix -r -l256 -b128MiB | tee -a RandomStrideSweep.txt #latency, 256B stride, 128MiB buffer
echo "\n1024B stride, 128MiB buffer (Latency Only):\n" >> RandomStrideSweep.txt
sudo ./mlc --latency_matrix -r -l1024 -b128MiB | tee -a RandomStrideSweep.txt #latency, 1024B stride, 128MiB buffer

#3.) Read/write mix sweep: compare throughput and latency under the required mixes.
echo "100% read:\n" >> ReadWriteMixSweep.txt
sudo ./mlc --loaded_latency -R | tee -a ReadWriteMixSweep.txt #100% read
echo "\n66.6/33.3 read/write:\n" >> ReadWriteMixSweep.txt
sudo ./mlc --loaded_latency -W2 | tee -a ReadWriteMixSweep.txt #close to 70/30 read/write, but actually 66.6/33.3 read/write
echo "\n50/50 read/write:\n" >> ReadWriteMixSweep.txt
sudo ./mlc --loaded_latency -W5 | tee -a ReadWriteMixSweep.txt #50/50 read/write
echo "\n100% write:\n" >> ReadWriteMixSweep.txt
sudo ./mlc --loaded_latency -W6 | tee -a ReadWriteMixSweep.txt #basically 100% write (non-temporal)

#4.) Intensity sweep: plot throughput vs. latency, identify the knee, and relate it to Little’s Law.
#use sudo ./mlc --loaded_latency -d(injected latency ns)
echo "Note: Throughput is Bandwidth in GB/s, plot with latency, not injected delay.\n" >> IntensitySweep.txt
sudo ./mlc --loaded_latency | tee -a IntensitySweep.txt #use the default for sweeping, it is easier

#for MLC, the ElementWiseMultiply kernel would have been good

#then set up for kernel benchmarking

#Kernel/C++ Benchmarking (Parts 5-8):
#5.) Working-set sweep: annotate L1/L2/LLC/DRAM transition regions.
#-->AXPY Kernel
#6.) Cache-miss impact: vary footprint or access pattern and correlate perf counters with runtime.
#-->AXPY Kernel
#7.) TLB-miss impact: vary page locality and evaluate huge pages where possible.
#-->Dot Product Reduction Kernel
#8.) Show one benchmark pitfall, such as accidentally measuring cache reuse instead of DRAM, failing to pin threads, or allowing frequency scaling to distort results.

#call the .cpp file that does #5-8 for you
echo "Running .cpp file for #5-7 (WorkingSet, CacheMiss, TLBMiss), see .csv files for results (won't be printed here): \n"
gcc project2cache.cpp -lstdc++ -o project2cache.exe #compile
sudo ./project2cache.exe #run

#NOTE: how to compile and run properly: gcc project2cache.cpp -lstdc++ -o project2cache.exe
#that -lstdc++ is what causes the linker for the libraries to work
#NOTE: you must also run this with sudo or else the perf benchmark counters won't work


#undoing the testing environment to return the system back to normal config
#puts intel pstate back to active mode, which allows for dynamic frequency scaling
echo active | tee /sys/devices/system/cpu/intel_pstate/status
cpupower frequency-set -g powersave
# Clear explicit frequency constraints
#cpupower frequency-set -u 100% -d 0% #this is redundant due to the change in governor
# 1. Re-enable C-states and clear latency constraints
cpupower idle-set -D 0ms
cpupower idle-set -E
#re-enable turbo boost
echo 0 | tee /sys/devices/system/cpu/intel_pstate/no_turbo
#then restart your computer to undo the rest
