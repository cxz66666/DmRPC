# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading
import paramiko

base = "/home/cxz/rmem"

init_build = True

# use for Nexus connect
client_machine1 = "192.168.189.7"
client_machine2 = "192.168.189.8"
client_machine3 = "192.168.189.11"

load_balance_machine = "192.168.189.9"

worker_machine1 = "192.168.189.12"
worker_machine2 = "192.168.189.13"
worker_machine3 = "192.168.189.14"

user = "cxz"
passwd = "cxz123"
output_file_format = "/home/cxz/lb_test_result/{}_b{}_t{}_c{}"
msg_size = [8, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 ]
max_concurrency = [128, 64, 64, 64, 64, 64, 64, 64, 16]
min_concurrency = [128, 64, 64, 64, 64, 64, 64, 64, 16]

# num_threads = [1, 2, 4, 6, 8]
num_threads = [1, 4]
concurrency = [1, 2, 4, 8, 16, 32, 64, 128]

common_timeout = 60

self_index_list = [0, 1, 3, 2, 9, 14, 4, 5, 6]
forward_index_list = [2, 9, 14, 0, 0, 0, 0, 0, 0]
backward_index_list = [0, 0, 0, 0, 0, 0, 2, 9, 14]


def make_and_clean(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo img_transcode > ./cn/app/build_app && echo lb_test > ./cn/app/img_transcode/build_app && ls "
        "./cn/app/ && cp ./cn/app/app_process_file_erpc ./cn/app/app_process_file && rm -rf "
        "./build && mkdir build && cd build && cmake .. && make -j".format(base)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def common_run(ssh, program, self_index, thread_num, offset, extra=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} $(cat ../cn/app/img_transcode/{1}/config) "
        "--server_forward_index={2} "
        "--server_backward_index={3} "
        "--server_index={4} "
        "--client_num={5} "
        "--server_forward_num={5} "
        "--server_backward_num={5} "
        "--server_num={5} "
        "--bind_core_offset={6} "
        "--timeout_second={8} "
        "{7}".format(base, program, forward_index_list[self_index], backward_index_list[self_index],
                     self_index_list[self_index], thread_num, offset, extra, common_timeout)
        , get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def pcm_run(ssh, timeout, output_file):
    stdin, stdout, stderr = ssh.exec_command(
        "sudo timeout -s SIGTERM {0} sudo pcm-memory 0.1 -csv={1}".format(timeout, output_file), get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def ssh_connect(ip, user, passwd):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, passwd)
    return ssh


extra_load_balance1 = "--load_balance_servers_index=4 --load_balance_backs_index=0"
extra_load_balance2 = "--load_balance_servers_index=5 --load_balance_backs_index=1"
extra_load_balance3 = "--load_balance_servers_index=6 --load_balance_backs_index=3"

extra_client = "--test_loop=20 --concurrency={0} --block_size={1} --latency_file={2} --bandwidth_file={3} " \
               "--extra_flags={4} "
extra_worker = "--numa_worker_node=0 --worker_bind_core_offset=0"

if __name__ == '__main__':
    ssh_client1 = ssh_connect(client_machine1, user, passwd)
    ssh_client2 = ssh_connect(client_machine2, user, passwd)
    ssh_client3 = ssh_connect(client_machine3, user, passwd)

    ssh_load_balance = ssh_connect(load_balance_machine, user, passwd)

    ssh_worker1 = ssh_connect(worker_machine1, user, passwd)
    ssh_worker2 = ssh_connect(worker_machine2, user, passwd)
    ssh_worker3 = ssh_connect(worker_machine3, user, passwd)

    if init_build:
        t0 = threading.Thread(target=make_and_clean, args=(ssh_client1,))
        t1 = threading.Thread(target=make_and_clean, args=(ssh_client2,))
        t2 = threading.Thread(target=make_and_clean, args=(ssh_client3,))

        t3 = threading.Thread(target=make_and_clean, args=(ssh_load_balance,))

        t4 = threading.Thread(target=make_and_clean, args=(ssh_worker1,))
        t5 = threading.Thread(target=make_and_clean, args=(ssh_worker2,))
        t6 = threading.Thread(target=make_and_clean, args=(ssh_worker3,))

        t0.start()
        t1.start()
        t2.start()
        t3.start()
        t4.start()
        t5.start()
        t6.start()
        t0.join()
        t1.join()
        t2.join()
        t3.join()
        t4.join()
        t5.join()
        t6.join()

    for t_i in num_threads:
        for index, m_i in enumerate(msg_size):
            for c_i in concurrency:
                if c_i > max_concurrency[index] or c_i < min_concurrency[index]:
                    print("skip thread {} msg_size {} concurrency {}".format(t_i, m_i, c_i))
                    continue

                t0 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_client1, "lb_client", 0, t_i, 0,
                                          extra_client.format(c_i, m_i,
                                                              output_file_format.format("lat", m_i, t_i, c_i),
                                                              output_file_format.format("band", m_i, t_i, c_i), 0)
                                      ))
                t1 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_client2, "lb_client", 1, t_i, 0,
                                          extra_client.format(c_i, m_i,
                                                              output_file_format.format("lat", m_i, t_i, c_i),
                                                              output_file_format.format("band", m_i, t_i, c_i), 0)
                                      ))
                t2 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_client3, "lb_client", 2, t_i, 0,
                                          extra_client.format(c_i, m_i,
                                                              output_file_format.format("lat", m_i, t_i, c_i),
                                                              output_file_format.format("band", m_i, t_i, c_i), 0)
                                      ))

                t3 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_load_balance, "lb_lb", 3, t_i, 0, extra_load_balance1
                                      ))

                t4 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_load_balance, "lb_lb", 4, t_i, t_i * 2, extra_load_balance2
                                      ))
                t5 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_load_balance, "lb_lb", 5, t_i, t_i * 4, extra_load_balance3
                                      ))

                t6 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_worker1, "lb_worker", 6, t_i, 0, extra_worker)
                                      )
                t7 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_worker2, "lb_worker", 7, t_i, 0, extra_worker)
                                      )
                t8 = threading.Thread(target=common_run,
                                      args=(
                                          ssh_worker3, "lb_worker", 8, t_i, 0, extra_worker)
                                      )

                t9 = threading.Thread(target=pcm_run, args=(
                    ssh_load_balance, common_timeout, output_file_format.format("pcm", m_i, t_i, c_i)
                ))

                t0.start()
                t1.start()
                t2.start()
                t3.start()
                t4.start()
                t5.start()
                t6.start()
                t7.start()
                t8.start()
                t9.start()

                t0.join()
                t1.join()
                t2.join()
                t3.join()
                t4.join()
                t5.join()
                t6.join()
                t7.join()
                t8.join()
                t9.join()

                print("finish: thread {} msg_size {} concurrency_size {}\n".format(t_i, m_i, c_i))

    ssh_client1.close()
    ssh_client2.close()
    ssh_client3.close()
    ssh_load_balance.close()
    ssh_worker1.close()
    ssh_worker2.close()
    ssh_worker3.close()
