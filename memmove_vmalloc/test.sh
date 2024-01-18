#! bin/bash

insmod /home/jhb/dsa_sample/memmove_vmalloc/vmalloc_to_userspace_dst.ko
insmod /home/jhb/dsa_sample/memmove_vmalloc/vmalloc_to_userspace.ko
insmod /home/jhb/dsa_sample/memmove_vmalloc/kmalloc_to_userspace.ko
insmod /home/jhb/dsa_sample/memmove_vmalloc/kmalloc_to_userspace_dst.ko
read -p "line: " line
dmesg | tail -n $line > test_$line

for ((i=0; i<10; i++));
do
    /home/jhb/dsa_sample/memmove_vmalloc/mmap_to_userspace_soft_v | tail -n 3 | head -n 1 >> test
    echo $i
    sleep 1
done

for ((i=0; i<10; i++));
do
    /home/jhb/dsa_sample/memmove_vmalloc/mmap_to_userspace_soft_k | tail -n 3 | head -n 1 >> test
    echo $i
    sleep 1
done

for ((i=0; i<10; i++));
do
    /home/jhb/dsa_sample/memmove_vmalloc/mmap_to_userspace_dsa_v | tail -n 2 | head -n 1 >> test
    echo $i
    sleep 1
done

for ((i=0; i<10; i++));
do
    /home/jhb/dsa_sample/memmove_vmalloc/mmap_to_userspace_dsa_k | tail -n 2 | head -n 1 >> test
    echo $i
    sleep 1
done

rmmod vmalloc_to_userspace_dst
rmmod vmalloc_to_userspace
rmmod kmalloc_to_userspace
rmmod kmalloc_to_userspace_dst