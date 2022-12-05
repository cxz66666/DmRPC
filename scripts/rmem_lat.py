# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading

import paramiko

base = "/path/to/baseurl"

# use for Nexus connect
client_machine = "192.168.189.9"

user = "user"
passwd = "passwd"
output_file_format = "/path/to/result-lat/{}-{}-{}-{}"
msg_size = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576,
            2097152, 4194304]
max_concurrency = [128, 128, 128, 128, 128, 128, 128, 64, 64, 64, 16, 8, 4, 4, 4, 2, 2, 1]
min_concurrency = [32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 4, 2, 1, 1, 1, 1, 1, 1]

num_threads = [1, 2, 4, 8, 12]
concurrency = [1, 2, 4, 8, 16, 32, 64, 128]


def run_process_lat(ssh, block_size, concurrency_num, read_output_file, write_output_file):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo {1} > ./cn/app/build_app && cd build && cmake .. && make -j &&  sudo ./{1}  $(cat "
        "../cn/app/{1}/config) --block_size {3} --client_thread_num=1 --concurrency={4}  --read_result_file={5} "
        "--write_result_file={6}".format(
            base, "latency_test", passwd, block_size, concurrency_num, read_output_file, write_output_file))
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

    for m_i in msg_size:
        for c_i in concurrency:
            t1 = threading.Thread(target=run_process_lat,
                                  args=(
                                      ssh_client, m_i, c_i, output_file_format.format("read", m_i, 1, c_i),
                                      output_file_format.format("write", m_i, 1, c_i)))
            t1.start()
            t1.join()
            print("finish: thread {} msg_size {} concurrency_size {}\n".format(1, m_i, c_i))

    ssh_client.close()
