# script.py
import ray
import numpy
import sys
import copy
import time
import threading

time.time()
ray.init()

target1 = numpy.zeros(4000)
target1 = 1
tmp_arr = []
tmp_arr.extend(range(0, 3600))
target2 = copy.deepcopy(tmp_arr)

create_file_size = 100


def thread_run(thread_id):
    print(thread_id, " begin")

    timers1 = 0

    test_loop = create_file_size * 500

    ray_refs = []
    timer1 = time.time()
    for i in range(0, test_loop):
        ray.put(1)

    timers1 = (time.time() - timer1) * 1e6
    print(thread_id, timers1 / test_loop)

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
