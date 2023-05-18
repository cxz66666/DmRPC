# script.py
import ray
import numpy
import sys
import copy
import time
import threading

time.time()
ray.init(address='auto', _node_ip_address='10.0.0.3')

machine2_id = "e5099a23c0b8124731d77cc9f7cfd0e6bd5d918b644e5673c6e15704"
machine3_id = "5f1414ab7308c89fa91e16a84a6e44c7ed0f46729f9d714198854bf0"
target1 = numpy.zeros(4000)
# tmp_arr = []
# tmp_arr.extend(range(0,3600))
# target2 = copy.deepcopy(tmp_arr)

create_file_size = 100
file_buffer = [copy.deepcopy(target1) for i in range(create_file_size)]

copy_src = numpy.zeros(500)

result = 0


@ray.remote
def node_affinity_func2(a):
    a = a.copy()
    write_num = 6
    for num in range(write_num):
        a[num * 500:(num + 1) * 500] = copy_src
    # a[0] = 1
    return 1


def thread_run(thread_id):
    global result
    print(thread_id, " begin")

    timers1 = 0
    timers2 = 0
    timers3 = 0

    test_loop = create_file_size * 100

    time_begin = time.time()

    ray_refs = []
    for i in range(0, test_loop):
        timer1 = time.time()
        object_ref = ray.put(file_buffer[i % create_file_size])
        timer2 = time.time()
        ref1 = node_affinity_func2.options(
            scheduling_strategy=ray.util.scheduling_strategies.NodeAffinitySchedulingStrategy(
                node_id=machine2_id,
                soft=False,
            )
        ).remote(object_ref)
        timer3 = time.time()
        ray_refs.append(ref1)
        timers1 = timers1 + (timer2 - timer1) * 1e6
        timers2 = timers2 + (timer3 - timer2) * 1e6

    ray.wait(object_refs=ray_refs, num_returns=test_loop)
    timers3 = (time.time() - time_begin) * 1e6

    print(thread_id, timers1 / test_loop, timers2 / test_loop, timers3 / test_loop)
    result = result + 1e6 * test_loop / timers3

    print(thread_id, " end")


threads = []
for i in range(1):
    threads.append(
        threading.Thread(target=thread_run, args=(i,))
    )

for t in threads:
    t.start()

for t in threads:
    t.join()

print("result", result)
