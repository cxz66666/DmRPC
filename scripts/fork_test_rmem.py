# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading
import time

import paramiko

base = "/home/cxz/rmem"

init_build = True

# use for Nexus connect
client_machine = "192.168.189.9"
server_machine = "192.168.189.11"
user = "cxz"
passwd = "cxz123"
output_file_format = "/home/cxz/fork_test_rmem_result/{}_w{}_s{}_cow{}"
zero_copys = [0, 1]

msg_size = 32768

num_write = [1, 2, 3, 4, 5, 6, 7, 8]
write_page_size = [4, 1024, 2048, 4096]
num_threads = [1]

common_timeout = 20

self_index_list = [0, 5]
forward_index_list = [5, 0]
backward_index_list = [0, 0]

extra_client = "--latency_file={0} --bandwidth_file={1} --rmem_self_index={2} --rmem_server_index={3} --block_size={4}"
extra_server = "--block_size={0} --rmem_self_index={1} --rmem_server_index={2} --write_num={3} --write_page_size={4}"

memory_node_alloc_gb = 16
memory_node_thread = 12
memory_node_ips = "192.168.189.8"
memory_node_port = 31851


def make_and_clean(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo fork_test > ./cn/app/build_app && cp "
        "./cn/app/app_process_file_fork_test ./cn/app/app_process_file && rm -rf "
        "./build && mkdir build && cd build && cmake .. && make -j".format(base)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def client_run(ssh, program, self_index, thread_num, offset, no_zero_copy, extra=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} $(cat ../cn/app/fork_test/fork_test_rmem/config1) "
        "--server_forward_index={2} "
        "--server_backward_index={3} "
        "--server_index={4} "
        "--client_num={5} "
        "--server_forward_num={5} "
        "--server_backward_num={5} "
        "--server_num={5} "
        "--bind_core_offset={6} "
        "--timeout_second={8} "
        "{7} ".format(base, program, forward_index_list[self_index], backward_index_list[self_index],
                      self_index_list[self_index], thread_num, offset, extra, common_timeout)
        , get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def server_run(ssh, program, self_index, thread_num, offset, no_zero_copy, extra=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} $(cat ../cn/app/fork_test/fork_test_rmem/config2) "
        "--server_forward_index={2} "
        "--server_backward_index={3} "
        "--server_index={4} "
        "--client_num={5} "
        "--server_forward_num={5} "
        "--server_backward_num={5} "
        "--server_num={5} "
        "--bind_core_offset={6} "
        "--timeout_second={8} "
        "{7} ".format(base, program, forward_index_list[self_index], backward_index_list[self_index],
                      self_index_list[self_index], thread_num, offset, extra, common_timeout)
        , get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def memory_run(ssh, program, size_gb, self_ip, self_udp_port, thread_num, no_zero_copy=0):
    extra = ""
    if no_zero_copy == 1:
        extra = "--rmem_copy"
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} "
        "--rmem_size={2} "
        "--rmem_server_ip={3} "
        "--rmem_server_thread={4} "
        "--rmem_server_udp_port={5} "
        "--timeout_second={6} {7}".format(base, program, size_gb, self_ip, thread_num, self_udp_port,
                                          common_timeout + 10, extra)
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


if __name__ == '__main__':
    ssh_client = ssh_connect(client_machine, user, passwd)
    ssh_server = ssh_connect(server_machine, user, passwd)
    ssh_memory = ssh_connect(memory_node_ips, user, passwd)
    if init_build:
        t0 = threading.Thread(target=make_and_clean, args=(ssh_client,))
        t1 = threading.Thread(target=make_and_clean, args=(ssh_server,))
        m1 = threading.Thread(target=make_and_clean, args=(ssh_memory,))
        t0.start()
        m1.start()
        t1.start()

        t0.join()
        m1.join()
        t1.join()

    for w_i in num_write:
        for s_i in write_page_size:
            for t_i in num_threads:
                for z_i in zero_copys:
                    t0 = threading.Thread(target=client_run,
                                          args=(
                                              ssh_client, "fork_test_rmem_client", 0, t_i, 0, z_i, extra_client.format(
                                                  "latency.txt",
                                                  output_file_format.format("bw", w_i, s_i, z_i),
                                                  3, 2, msg_size
                                              )))
                    t1 = threading.Thread(target=server_run,
                                          args=(
                                              ssh_server, "fork_test_rmem_server", 1, t_i, 6, z_i,
                                              extra_server.format(msg_size, 6, 2, w_i, s_i )))

                    m1 = threading.Thread(target=memory_run, args=(
                        ssh_memory, "rmem_mn", memory_node_alloc_gb, memory_node_ips,
                        memory_node_port, memory_node_thread, z_i
                    ))
                    m1.start()
                    time.sleep(10)

                    t1.start()
                    t0.start()
                    t0.join()
                    t1.join()
                    m1.join()
                    print("finish: thread {} cow {} num_write {}  write_page_size {}\n".format(t_i, z_i, w_i,s_i))

    ssh_client.close()
