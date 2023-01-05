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

user = "cxz"
passwd = "cxz123"
output_file_format = "/home/cxz/fork_speed_cxl_result/{}_b{}_t{}_cow{}"
msg_size = [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288]

zero_copys = [0, 1]

# num_threads = [1, 2, 3, 4, 6, 8]
num_threads = [1, 12]

common_timeout = 40


def make_and_clean(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo fork_speed > ./cn/app/build_app && cp "
        "./cn/app/app_process_file_fork_speed ./cn/app/app_process_file && rm -rf "
        "./build && mkdir build && cd build && cmake .. && make -j".format(base)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def common_run(ssh, program, thread_num, block_size, latency_file, bandwidth_file, no_zero_copy=0):
    extra = ""
    if no_zero_copy == 1:
        extra = "--no_cow"
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && sudo ./{1} $(cat ../cn/app/fork_speed/config2) "
        "--client_thread_num={2} "
        "--block_size={3} "
        "--latency_file={4} "
        "--bandwidth_file={5} {6}".format(base, program, thread_num,
                                          block_size, latency_file, bandwidth_file, extra)
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


if __name__ == '__main__':
    ssh_client = ssh_connect(client_machine, user, passwd)

    if init_build:
        t0 = threading.Thread(target=make_and_clean, args=(ssh_client,))
        t0.start()

        t0.join()

    for t_i in num_threads:
        for index, m_i in enumerate(msg_size):
            for z_i in zero_copys:
                t0 = threading.Thread(target=common_run, args=(ssh_client, "fork_speed_cxl", t_i, m_i,
                                                               output_file_format.format("lat", m_i, t_i, z_i),
                                                               output_file_format.format("bw", m_i, t_i, z_i), z_i))

                m1 = threading.Thread(target=pcm_run, args=(
                    ssh_client, common_timeout, output_file_format.format("pcm", m_i, t_i, z_i)))

                m1.start()
                time.sleep(2)
                t0.start()
                t0.join()
                m1.join()

                print("finish: thread {} msg_size {} cow {} \n".format(t_i, m_i, z_i))

    ssh_client.close()
