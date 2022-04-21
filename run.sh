#!/usr/bin/env bash

args=(--debug-flags=CSIRowhammer
      gem5/configs/CSIRowhammer/fs.py
      --cpu-type=TimingSimpleCPU
      --kernel linux/vmlinux
      
      # Not required because the correction can already be tested while the kernel is booting after
      # the exception handler is configured.
      # --disk-image disks/debian.img
      
      --command-line 'earlycon=console_msg_format=syslog nokaslr norandmaps panic=-1 printk.devkmsg=on printk.time=y rw console=ttyS0 - lkmc_home=/lkmc earlyprintk=ttyS0 lpj=7999923 root=/dev/sda ttyS0'
      --mem-size 100MB
      --caches
      --l2cache
      --cpu-clock=3GHz
      
      --mem-type=DDR4_2400_8x8
      --mem-channels 1
      --mem-ctrl=csi
      
      # wait for 300000 (exception handler ready) memory accesses then flip a bit and additionally flip a bit at physical address 0x105d300, ...
      --mem-ctrl-fail='300000,-0x105d300,301000,302000,303000,304000,305000'
      
      # set this to a higher value for benchmarks, for error correction tests we set it to 0
      --mem-ctrl-mac-latency=0ns
)
      
gem5/build/X86/gem5.opt "${args[@]}"

