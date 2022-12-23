import paramiko

dst_ips = ["192.168.189.7", "192.168.189.8", "192.168.189.11", "192.168.189.12", "192.168.189.13", "192.168.189.14"]

base = "/home/cxz/rmem"
user = "cxz"
passwd = "cxz123"


def delete_file(ssh):
    stdin, stdout, stderr = ssh.exec_command(
        "rm -rf {0}".format(base)
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
    for ip in dst_ips:
        ssh_client = ssh_connect(ip, user, passwd)
        delete_file(ssh_client)
        ssh_client.close()
        print("delete file on {} successfully".format(ip))
