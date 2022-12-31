import paramiko

src_ip = "192.168.189.9"
dst_ips = ["192.168.189.7", "192.168.189.8", "192.168.189.11", "192.168.189.12", "192.168.189.13", "192.168.189.14"]

base = "/home/cxz/rmem"
dst_base = "/home/cxz/"
user = "cxz"
passwd = "cxz123"


def copy_file(ssh, dst):
    stdin, stdout, stderr = ssh.exec_command(
        "sshpass -p {0} scp -prq -o StrictHostKeyChecking=no {1} {2}@{3}:{4}".format(passwd, base, user, dst,dst_base)
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
    ssh_client = ssh_connect(src_ip, user, passwd)
    for ip in dst_ips:
        copy_file(ssh_client, ip)
        print("copy to {} done".format(ip))
    ssh_client.close()
