import threading
import paramiko

dst_ips = ["192.168.189.7","192.168.189.9", "192.168.189.8", "192.168.189.11", "192.168.189.12"]
# dst_ips = ["192.168.189.9"]

base = "/home/cxz/rmem/build/erpc_dpdk_daemon"
user = "cxz"
passwd = "cxz123"


def start_daemon(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "sudo {0}".format(base)
        , get_pty=True
    )
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
    threads_list = []
    ssh_list = []
    for ip in dst_ips:
        ssh_client = ssh_connect(ip, user, passwd)
        ssh_list.append(ssh_client)
        t1 = threading.Thread(target=start_daemon, args=(ssh_client,))
        threads_list.append(t1)

    for t in threads_list:
        t.start()

    for t in threads_list:
        t.join()
