# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading
import paramiko
import time

base = "/home/cxz/study/DmRPC"

init_build = False

client_machine = "172.25.2.23"
server_machine = "172.25.2.24"

user = "cxz"
passwd = "cxz123"

output_file_format = "/home/cxz/cloudnic_compress/{}_t{}_c{}"

def make_and_clean(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo cloudnic > ./cn/app/build_app && rm -rf "
        "./build && mkdir build && cd build && PKG_CONFIG_PATH=:/opt/mellanox/doca/lib/x86_64-linux-gnu/pkgconfig:/opt/mellanox/flexio/lib/pkgconfig:/opt/mellanox/grpc/lib/pkgconfig:/opt/mellanox/dpdk/lib/x86_64-linux-gnu/pkgconfig cmake .. && make -j".format(base)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)

def common_run(ssh, program, concurrency, self_addr, thread_num, timeout, lat_file, bw_file):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && echo {9} | sudo -S ./{1} $(cat ../cn/app/cloudnic/{1}/config) "
        "--concurrency={2} "
        "--server_addr={3} "
        "--remote_addr={4} "
        "--client_num={5} "
        "--server_num={5} "
        "--latency_file={6} "
        "--bandwidth_file={7} "
        "--timeout_second={8} ".format(base, program, concurrency, self_addr, server_machine+":31851", thread_num, lat_file, bw_file, timeout, passwd)
        , get_pty=True)
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)    

client_timeout = 15
server_timeout = 30

num_threads = [1,2,3,4,5,6,7,8,9,10,11,12]
concurrency = [1, 2, 4, 8, 16, 32]


def create_dir(ssh, filename):
    stdin, stdout, stderr = ssh.exec_command(
        "mkdir -p $(dirname {0})".format(filename)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def ssh_connect(ip, user):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, None, key_filename="/home/cxz/.ssh/id_ed25519")
    return ssh


if __name__ == '__main__':
    ssh_client = ssh_connect(client_machine, user)
    ssh_server = ssh_connect(server_machine, user)
    if init_build:
        t0 = threading.Thread(target=make_and_clean,
                                args=(ssh_client,))
        t0.start()
        t0.join()

    create_dir(ssh_client, output_file_format.format(1,1,1))
    create_dir(ssh_server, output_file_format.format(1,1,1))
    print("Start testing")
    for t_i in num_threads:
        for c_i in concurrency:

            t_s = threading.Thread(target=common_run,
                                args=(ssh_server, "compress_server", c_i, server_machine+":31851", t_i, server_timeout, output_file_format.format("lat", t_i, c_i), output_file_format.format("bw", t_i, c_i)))
            t_c = threading.Thread(target=common_run,
                                args=(ssh_client, "compress_client", c_i, client_machine+":31851", t_i, client_timeout, output_file_format.format("lat", t_i, c_i), output_file_format.format("bw", t_i, c_i)))
            
            t_s.start()
            t_c.start()

            t_c.join()
            t_s.join()

            print("Thread: {0}, Concurrency: {1}".format(t_i, c_i))