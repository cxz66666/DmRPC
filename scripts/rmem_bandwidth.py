# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.
import threading

import paramiko

base = "/home/cxz/rmem"
program = ["bandwidth_read_test", "bandwidth_write_test"]

client_machine = "192.168.189.9"

user = "cxz"
passwd = "cxz123"
output_file_format = "/home/cxz/result/{}_b{}_t{}_c{}"
msg_size = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576,
            2097152, 4194304]
max_concurrency = [128, 128, 128, 128, 128, 128, 128, 64, 64, 64, 16, 8, 4, 4, 4, 2, 2, 1]
min_concurrency = [32, 32, 32, 32, 32, 32, 32, 16, 16, 16, 4, 2, 1, 1, 1, 1, 1, 1]

num_threads = [1, 2, 4, 8, 12]
concurrency = [1, 2, 4, 8, 16, 32, 64, 128]


def run_process_read(ssh, block_size, num_client_threads, concurrency_num, output_file):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo {1} > ./cn/app/build_app && cd build && cmake .. && make -j &&  sudo ./{1}  $(cat "
        "../cn/app/{1}/config) --block_size {3} --client_thread_num={4} --concurrency={5}  --result_file={6} "
        "--alloc_size={7} --numa_node_user_thread={8}".format(
            base, program[0], passwd, block_size, num_client_threads, concurrency_num, output_file,
            min(48, num_client_threads), 0 if num_client_threads <= 6 else 1))
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def run_process_write(ssh, block_size, num_client_threads, concurrency_num, output_file):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && echo {1} > ./cn/app/build_app && cd build && cmake .. && make -j &&  sudo ./{1}  $(cat "
        "../cn/app/{1}/config) --block_size {3} --client_thread_num={4} --concurrency={5}  --result_file={6} "
        "--alloc_size={7} --numa_node_user_thread={8}".format(
            base, program[1], passwd, block_size, num_client_threads, concurrency_num, output_file,
            min(48, num_client_threads), 0 if num_client_threads <= 6 else 1))
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


# 请求服务器获取信息
# def Kp2pGET():
#     ssh = paramiko.SSHClient()
#     ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
#     ssh.connect('192.168.189.8', 22, 'cxz', 'cxz123')
#     stdin, stdout, stderr = ssh.exec_command('pwd')
#     str1 = stdout.read().decode('utf-8')
#     print(str1)
#     ssh.close()


def ssh_connect(ip, user, passwd):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, passwd)
    return ssh


if __name__ == '__main__':
    ssh_client = ssh_connect(client_machine, user, passwd)

    for t_i in num_threads:
        for index, m_i in enumerate(msg_size):
            for c_i in concurrency:
                if c_i > max_concurrency[index] or c_i < min_concurrency[index]:
                    print("skip thread {} msg_size {} concurrency {}".format(t_i, m_i, c_i))
                    continue
                t1 = threading.Thread(target=run_process_read,
                                      args=(
                                          ssh_client, m_i, t_i, c_i, output_file_format.format("read", m_i, t_i, c_i)))
                t1.start()
                t1.join()
                print("finish: thread {} msg_size {} concurrency_size {}\n".format(t_i, m_i, c_i))

    for t_i in num_threads:
        for index, m_i in enumerate(msg_size):
            for c_i in concurrency:
                if c_i > max_concurrency[index] or c_i < min_concurrency[index]:
                    print("skip thread {} msg_size {} concurrency {}".format(t_i, m_i, c_i))
                    continue
                t1 = threading.Thread(target=run_process_write,
                                      args=(
                                          ssh_client, m_i, t_i, c_i, output_file_format.format("write", m_i, t_i, c_i)))
                t1.start()
                t1.join()
                print("finish: thread {} msg_size {} concurrency_size {}\n".format(t_i, m_i, c_i))

    ssh_client.close()
