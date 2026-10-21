# Advanced-Computer-Systems-Class-Wide-Projects----Project-2-Cache-and-Memory-Performance-Profiling

Measurement Discipline (System Configuration):
- System Specs:
- CPU: Intel Core i5 1135G7, 4c/8t, 400MHz-4.20GHz
- --> Turbo boost disabled via Intel P_State governor modifications
- --> Deliverables #5-8 pinned to thread 0 (working set sweep, cache miss impact, TLB miss impact, failed benchmark)
- --> Cache working sets were based on cache size information pulled from Intel Ark website (along with some linux commands), see further below
- RAM: 8GB LPDDR4X 4267MHz (Dual Channel)
- OS: Linux Debian Trixie 13.7.0
- Compiler/Tools:
- GCC 14.2.0 (Default with Debian 13)
- Governor: intel cpu_freq
- VSCode IDE

Datatype chosen: Long (int) [usually 64-bit]

Cache Working set sizes: (these sizes fit within these cache hierarchies)
- For long (usually 64-bit):
- --> 2,048 Longs = ~16KiB (L1-I, per core)
- --> 4,608 Longs = ~36KiB (L1-D, per core)
- --> 131,072 Longs = ~1MiB (L2, per core)
- --> 786,432 Longs = ~6MiB (L3, shared)
- --> 8,388,608 Longs = ~64MiB (DRAM, no caching)

According to Intel's website for the i5 1135g7 + some linux commands, I was able to derive the cache hierarchy for this CPU:
- L1-I: 128KiB (32KiB per core)
- L1-D: 192KiB (48KiB per core)
- L2: 5MiB (1.25MiB per core)
- L3 "Smart": 8MiB (shared)

Strides:
- For #1-4: 8 Longs (64B), 32 Longs (256B), 128 Longs (1024B)
- For #6: 0 Longs (no stride), 8 Longs (64B), 32 Longs (256B), 128 Longs (1024B)
- For #7: 1 Long (8B), 512 Longs (4096B), 262,144 Longs (2MiB)

Read/Write Ratios:
- 100% Reads
- 66.6% Reads / 33.3% Writes (as close as I could get to 70/30)
- 50% Reads / 50% Writes
- 100% Writes

Core compromise: Intel MLC:
- Intel MLC is limited in how it handles system configurations, so I cannot disable SMT/Hyperthreading when running it, nor can I pin cores/threads to it either.
- Intel MLC fails in certain custom argument configurations, hence will be labeled N/A if needed
- Intel MLC cannot track bandwidth when running granularity sweeps, it also cannot track bandwidth for stride sweeps of random access nature.
- Here is some core information when running Intel MLC:
- --> Turbo Boost was disabled
- --> Idle/Sleep C states were disabled
- --> Frequency was pinned to 1.40GHz
- --> Hyperthreading/SMT is still enabled
- --> No core/thread pinning was introduced

A secondary compromise: perf:
- perf library cannot measure L2 hits and misses without a custom MSR, which makes it even less reproducible as it varies not only by vendor (Intel,), but also by microarchitecture, so that means that even if you have an intel cpu, if it is of a different generation or architectural tier, it will potentially have a different L2 metric, so it would break across too many systems

Pre-experiment Hypothesis:
- I expect that the smaller faster caches will result in higher throughput and effective bandwidth when minimizing strides.
- --> When sweeping through working set sizes: It will transition into larger level caches, which means that the hitrate of L1 cache will go dramatically down, and once working set sizes surpass the size of a certain level of cache, that cache level's hitrate will nosedive.
- --> Impact of cache misses: More cache misses will dramatically decrease effective bandwidth and increase latency.
- --> Impact of TLB misses: 
- --> Suspected benchmark failure: Core/thread pinning will not work on intel MLC

Data collected:
- For #1-4, I measured a couple things:
- --> Bandwidth (MB/s or GB/s)
- --> Idle Latency (ns)
- --> Loaded Latency (ns)
- For #5-7, I measured a few things:
- --> CPU Instructions (Count)
- --> CPU Cycles (Count)
- --> L1 Hitrate (%)
- --> LLC Hitrate (%) (usually L3 cache)
- --> L1 Misses (Count)
- --> LLC Misses (Count) (usually L3 cache)

AI Use Note:
- Tools used: Copilot (Desktop), Copilot (Bing), Gemini (Online)
- Copilot was used for:
- --> Initial code generation for benchmarking harness and perf calls (I modified it quite a bit)
- --> Looking up documentation regarding: performance profiling for intel_pstate governors, Intel MLC quirks, Intel MLC custom commands
- --> Support in the scripting (how to write to file output, how to elevate permissions, etc)
- --> Code verification (2nd opinion, as I check first)
- --> 2nd opinion on data results (I check first)
- --> bouncing ideas off eachother for possible explanations
- --> doing last minute polishing of my report explanations (I wrote the original explanations, but they were rough to read)
- Gemini was used for:
- --> Documentation on how to generate random numbers and random vectors
- --> Assisting with required libraries for perf
- Note: There are comments for parts of my code where I mention if it is AI generated, and what prompts were used. Although I still modify a lot of the AI-generated code

Another important source: https://linux.die.net
- I used the cpupower documentation there when configuring my cpu for the benchmarking script.

How to use:
- Download the required files and make sure they are all in the same folder directory:
- Required files:
- --> project2cache.cpp
- --> benchmarkscript.sh (you need sudo priviledges for this)
- --> you should have intel mlc here too
- Optional files: (these are for support)
- --> HowToFixCoreAllocationScript.txt (if your OS is masking cores, use this, tested on Debian)
- Step 1: Run the benchmarkscript.sh file in terminal
- Step 2: Look at the result files it generates for you (.txt and .csv files)
