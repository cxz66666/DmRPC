# script.py
import ray
import numpy
import sys
import copy
import time

time.time()
ray.init(address='auto', _node_ip_address='10.0.0.3')

machine2_id = "6b7cd9c28304b7cb0a279ba78ec0e5edc2f458cc809ec26e9b0bb2d9"
machine3_id = "fba1191831e905facc03d1f8b9bccb814a49b72e838271ea401d1f90"
target1 = numpy.zeros(4000)
# tmp_arr = []
# tmp_arr.extend(range(0,3600))
# target2 = copy.deepcopy(tmp_arr)

create_file_size = 100
file_buffer = [copy.deepcopy(target1) for i in range(create_file_size)]

copy_src = numpy.zeros(500)

write_num = 6

@ray.remote
def node_affinity_func2(a):
    a = a.copy()
    for num in range(write_num):
        a[num*500:(num+1)*500] = copy_src
    # a[0] = 1
    return 1


timers1 = 0
timers2 = 0
timers3 = 0


test_loop = create_file_size*100
for i in range(0,test_loop):
    timer1 = time.time()
    object_ref = ray.put(file_buffer[i%create_file_size])
    timer2 = time.time()
    ref1= node_affinity_func2.options(
        scheduling_strategy=ray.util.scheduling_strategies.NodeAffinitySchedulingStrategy(
            node_id=machine2_id,
            soft=False,
        )
    ).remote(object_ref)
    timer3 = time.time()
    ray.get(ref1)
    timers1 = timers1 + (timer2-timer1)*1e6
    timers2 = timers2 + (timer3-timer2)* 1e6
    timers3 = timers3 + (time.time()-timer3)* 1e6

print(timers1/test_loop,timers2/test_loop,timers3/test_loop)

