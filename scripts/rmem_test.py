# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading
import time

import paramiko

base = "/home/cxz/rmem"

init_build = True

# use for Nexus connect
client_machine = "192.168.189.7"
firewall_machine = "192.168.189.11"
load_balance_machine = "192.168.189.11"
img_server1_machine = "192.168.189.12"
img_server2_machine = "192.168.189.12"
worker1_machine = "192.168.189.13"
worker2_machine = "192.168.189.14"
memory_machine1 = "192.168.189.8"
memory_machine2 = "192.168.189.9"

memory_node_alloc_gb = 16
memory_node_thread = 12
memory_node_ips = ["192.168.189.8", "192.168.189.9"]
memory_node_ports = [31851, 31851]

user = "cxz"
passwd = "cxz123"
output_file_format = "/home/cxz/rmem_result/{}_b{}_t{}_w{}_c{}"
msg_size = [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576,
            2097152, 4194304]
file_map = {4096: "../scripts/echo/test_4k.bmp",
            8192: "../scripts/echo/test_8k.bmp",
            16384: "../scripts/echo/test_16k.bmp",
            32768: "../scripts/echo/test_32k.bmp",
            65536: "../scripts/echo/test_64k.bmp",
            131072: "../scripts/echo/test_128k.bmp",
            262144: "../scripts/echo/test_256k.bmp",
            524288: "../scripts/echo/test_512k.bmp",
            1048576: "../scripts/echo/test_1m.bmp",
            2097152: "../scripts/echo/test_2m.bmp",
            4194304: "../scripts/echo/test_4m.bmp",
            }
max_concurrency = [64, 64, 64, 16, 8, 4, 4, 4, 2, 2, 1]
min_concurrency = [16, 16, 16, 4, 2, 1, 1, 1, 1, 1, 1]

num_threads = [1, 2, 3, 4, 6, 8]
num_workers = [1, 2]
concurrency = [1, 2, 4, 8, 16, 32, 64, 128]

common_timeout = 60

self_index_list = [0, 1, 2, 3, 4, 5, 6]
forward_index_list = [1, 2, 0, 5, 6, 0, 0]
backward_index_list = [0, 0, 1, 2, 2, 3, 4]


def make_and_clean(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo img_transcode > ./cn/app/build_app && echo rmem > ./cn/app/img_transcode/build_app  && cp "
        "./cn/app/app_process_file_rmem ./cn/app/app_process_file && rm -rf "
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


def memory_run(ssh, program, size_gb, self_ip, self_udp_port, thread_num):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} "
        "--rmem_size={2} "
        "--rmem_server_ip={3} "
        "--rmem_server_thread={4} "
        "--rmem_server_udp_port={5} "
        "--timeout_second={6} ".format(base, program, size_gb, self_ip, thread_num, self_udp_port, common_timeout + 10)
        , get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def ssh_connect(ip, user, passwd):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, passwd)
    return ssh


extra_firewall = ""
extra_load_balance = "--load_balance_servers_index=3,4"
extra_img_server1 = "--img_servers_index=5"
extra_img_server2 = "--img_servers_index=6"

extra_client = "--test_loop=20 --concurrency={0} --test_bitmap_file={1} --latency_file={2} " \
               "--bandwidth_file={3} --rmem_self_index={4} --rmem_server_indexs={5} --rmem_session_num={6}"
extra_worker = "--resize_factor=0.1 --worker_num={0} --rmem_self_index={1}"

if __name__ == '__main__':
    ssh_client = ssh_connect(client_machine, user, passwd)
    ssh_firewall = ssh_connect(firewall_machine, user, passwd)
    ssh_load_balance = ssh_connect(load_balance_machine, user, passwd)
    ssh_img_server1 = ssh_connect(img_server1_machine, user, passwd)
    ssh_img_server2 = ssh_connect(img_server2_machine, user, passwd)
    ssh_worker1 = ssh_connect(worker1_machine, user, passwd)
    ssh_worker2 = ssh_connect(worker2_machine, user, passwd)

    ssh_memory1 = ssh_connect(memory_machine1, user, passwd)
    ssh_memory2 = ssh_connect(memory_machine2, user, passwd)

    if init_build:
        t0 = threading.Thread(target=make_and_clean, args=(ssh_client,))
        t1 = threading.Thread(target=make_and_clean, args=(ssh_firewall,))
        t2 = threading.Thread(target=make_and_clean, args=(ssh_load_balance,))
        t3 = threading.Thread(target=make_and_clean, args=(ssh_img_server1,))
        t4 = threading.Thread(target=make_and_clean, args=(ssh_img_server2,))
        t5 = threading.Thread(target=make_and_clean, args=(ssh_worker1,))
        t6 = threading.Thread(target=make_and_clean, args=(ssh_worker2,))
        m1 = threading.Thread(target=make_and_clean, args=(ssh_memory1,))
        m2 = threading.Thread(target=make_and_clean, args=(ssh_memory2,))
        t0.start()
        t1.start()
        t2.start()
        t3.start()
        t4.start()
        t5.start()
        t6.start()
        m1.start()
        m2.start()

        t0.join()
        t1.join()
        t2.join()
        t3.join()
        t4.join()
        t5.join()
        t6.join()
        m1.join()
        m2.join()

    for t_i in num_threads:
        for w_i in num_workers:
            if t_i < w_i:
                continue
            for index, m_i in enumerate(msg_size):
                for c_i in concurrency:
                    if c_i > max_concurrency[index] or c_i < min_concurrency[index]:
                        print("skip thread {} msg_size {} concurrency {}".format(t_i, m_i, c_i))
                        continue
                    t1 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_firewall, "firewall", 1, 1, 0, extra_firewall))
                    t2 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_load_balance, "load_balance", 2, 1, 2, extra_load_balance
                                          ))
                    t3 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_img_server1, "img_server", 3, 1, 0, extra_img_server1
                                          ))
                    t4 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_img_server2, "img_server", 4, 1, 2, extra_img_server2
                                          ))
                    t0 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_client, "client_rmem", 0, 1, 0,
                                              extra_client.format(c_i, file_map[m_i],
                                                                  output_file_format.format("lat", m_i, t_i, w_i, c_i),
                                                                  output_file_format.format("band", m_i, t_i, w_i, c_i),
                                                                  9, "7,8", t_i)
                                          ))
                    t5 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_worker1, "img_bitmap_worker_rmem", 5, 1, 0,
                                              extra_worker.format(w_i, 10))
                                          )
                    t6 = threading.Thread(target=common_run,
                                          args=(
                                              ssh_worker2, "img_bitmap_worker_rmem", 6, 1, 0,
                                              extra_worker.format(w_i, 11))
                                          )
                    m1 = threading.Thread(target=memory_run,
                                          args=(
                                              ssh_memory1, "rmem_mn", memory_node_alloc_gb, memory_node_ips[0],
                                              memory_node_ports[0], memory_node_thread
                                          ))
                    m2 = threading.Thread(target=memory_run,
                                          args=(
                                              ssh_memory2, "rmem_mn", memory_node_alloc_gb, memory_node_ips[1],
                                              memory_node_ports[1], memory_node_thread
                                          ))

                    m1.start()
                    m2.start()
                    time.sleep(10)

                    t1.start()
                    t2.start()
                    t3.start()
                    t4.start()
                    t5.start()
                    t6.start()
                    t0.start()

                    t0.join()
                    t1.join()
                    t2.join()
                    t3.join()
                    t4.join()
                    t5.join()
                    t6.join()

                    m1.join()
                    m2.join()

                    print("finish: thread {} msg_size {} concurrency_size {}\n".format(t_i, m_i, c_i))

    ssh_client.close()
    ssh_firewall.close()
    ssh_load_balance.close()
    ssh_img_server2.close()
    ssh_img_server1.close()
    ssh_worker1.close()
    ssh_worker2.close()
    ssh_memory1.close()
    ssh_memory2.close()
