import pyarrow.plasma as plasma
import time
import threading

client = plasma.connect("/tmp/plasma")

create_file_size = 100


def thread_run(thread_id):
    print(thread_id, " begin")

    timers1 = 0

    test_loop = create_file_size * 1000

    timer1 = time.time()
    for i in range(0, test_loop):
        client.put(i)

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
