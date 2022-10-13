# CSI:Rowhammer - Proof of Concept Code

This is the PoC implementation for the IEEE Security & Privacy 2023 paper [**CSI:Rowhammer - Cryptographic Security and Integrity against Rowhammer**](https://www.jonasjuffinger.com/papers/csirowhammer.pdf) by [Jonas Juffinger](https://jonasjuffinger.com), Lukas Lamster, [Andreas Kogler](https://andreaskogler.com), [Moritz Lipp](https://mlq.me/), [Maria Eichlseder](https://twitter.com/MariaEichlseder), [Daniel Gruss](https://gruss.cc).

This repository contains the correction as a search code used for the correction duration analysis in Section 6.2.2. and the modified gem5 and Linux kernel code for the performance evaluation and the evaluation of the whole system design.

# gem5

## Modified / Added Files
| File(s) | Description |
|:--------|:------------|
|`src/CSIRowhammer/CSIMemCtrl.*`|Contains the modfied memory controller that performs the computation and check of the integrity information.|
|`src/CSIRowhammer/`<wbr>`IntegrityInformation.*`|Functions to compute the MAC and parity bits.|
|`src/CSIRowhammerqarma64tc_7.h`|Official 64-bit QARMA software implementation.|
|`src/cpu/simple/timing.cc`|The TimingSimpleCPU with additions to raise the `CSIRowhammerMACMismatch` exception if the memory controller finds a MAC mismatch.|
|`src/arch/x86/tlb.cc`|Modified the TBL to allow accesses with physical addresses for the `csi_load` and `csi_xchg` instructions.|
|`src/arch/x86/isa/microops/`<wbr>`ldstop.isa`|Added the µops to load data and integrity information without raising an exception with the physical address|
|`src/arch/x86/isa/decoder/`<wbr>`two_byte_opcodes_evex.isa`|Added opcodes for the three `csi_` instructions|
|`src/arch/x86/isa/insts/simd512/`<wbr>`floating_point/arithmetic/vcsild.py`|Definition of the `csi_load` instruction|
|`src/arch/x86/isa/insts/simd512/`<wbr>`floating_point/arithmetic/vcsimac.py`|Definition of the `csi_mac` instruction|
|`src/arch/x86/isa/insts/simd512/`<wbr>`floating_point/arithmetic/vcsixchg.py`|Definition of the `csi_xchg` instruction|
|`src/arch/x86/insts/microavxop.cc`|Added "micro"code for the `csi_mac` and `csi_xchg` instruction.|
|`src/arch/x86/faults.hh`|Added the `CSIRowhammerMACMismatch` exception.|
|`configs/CSIRowhammer/fs.py`|Full-System configuration file supporting CSI:Rowhammer|

## Run

To run gem5 with the modified Linux kernel to test the error correction capabilities perform the following steps.

### Build

Build gem5 with `scons build/X86/gem5.opt -j {cpus}`, see [https://www.gem5.org/documentation/general_docs/building](https://www.gem5.org/documentation/general_docs/building).

Build the linux kernel, the `.config` file is in the repository.

### Command Line Arguments

The script to run the test is `run.sh`.
It calls gem5 with a variety of command line arguments.

| Argument | Description |
|:---------|:------------|
|`--debug-flags`|The `CSIRowhammer` flags adds all debug output of CSI:Rowhammer|
|`--mem-ctrl-fail`|Is used to flip bits in the DRAM at specific memory accesses or phyiscal addresses. It is a comma seperated list of positive and negative integer values<br>Positive values define the n-th memory access where a bitflip should be induced. Every memory access reaching the memory controller is counted.<br>Negative values define a physical address where a bit should be flipped right after a flip is induced at the n-th memory access. It must always follow a positive value.|
|`--mem-ctrl-mac-latency`|The simulated added latency due to the MAC computation. It can be zero for tests regarding the error detection and correction and should be set to higher value for performance impact simulations.|
|`--cpu-type`|`TimingSimpleCPU` for error detection and correction evaluation and `O3` for performance impact simulations|
|`--kernel`|Path to the `vmlinux` file.|
|`--mem-size`|`100MB` is enough for detection and correction evaluation, the performance impact simulations was run with `4GB` and two memory controllers.|
|`--disk-image`|The detection and correction evaluation does not require a disk image, because errors can be induced already while the kernel is booting after the exception handlers are set up.<br>For performance impact simulations, a disk image with a Linux distribution and the benchmarks is required.|

### Run

Test detection and correction
`--mem-ctrl-fail="300000"`

Test detection of nested exception handlers with the nesting bit
`--mem-ctrl-fail="300000,-0x105d300"`

# Linux Kernel

## Modified / Added Files
| File(s) | Description |
|:--------|:------------|
|`arch/x86/mm/csi_rowhammer.c`|Contains the main exception handler function, the correction as a search function and many helper functions for, e.g., calling the `csi_` instructions.|
|`arch/x86/mm/fault.c`|Added exception handler `exc_csi_correction`.|

The correction is always performed as a search. The function `advanced_correction` is filled with many `NOPs` that make it easy to induce a bit flip during a correction attempt to the test the nesting detection.


# Correction as a Search

The correction as a search simulation code for the analysis in Section 6.2.2 and Table 4. It simulates 10,000 runs of the correction loop with up to 8 bitflips. This takes many hours, for a quicker test the MAX_FLIPS can be reduced to 5 or 6 and the MONTE_CARLO_RUNS to 1000.
The code also corrects errors from broken connection and single bit errors. These would normaly corrected in hardware, but we do it here to check the correctness of our algorithms.

The whole correction function is less than 0x1500 bytes large, as can be checked with this command:
```
objdump -t correction_simulation | grep "F .text" | grep correct
```
