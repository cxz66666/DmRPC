import paramiko
import os

src_ip = "192.168.189.8"

base = "/tmp/"
user = "cxz"
passwd = "cxz123"
proto_name = "social_network.proto"
dst_file_name = "social_network.pb.*"

proto_source_dir = os.path.join("/home/cxz/rmem","cn/app/social_network")
proto_source = os.path.join(proto_source_dir, proto_name)
print(proto_source)

def generate(ssh,name):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && protoc --cpp_out=. {1}".format(base,name)
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
    os.system("sshpass -p {0} scp -prq -o StrictHostKeyChecking=no {1} {2}@{3}:{4}".format(passwd, proto_source, user, src_ip,base))
    generate(ssh_client,proto_name)
    os.system("sshpass -p {0} scp -prq -o StrictHostKeyChecking=no {2}@{3}:{4} {1}".format(passwd, proto_source_dir, user, src_ip,base+dst_file_name))
    ssh_client.close()
