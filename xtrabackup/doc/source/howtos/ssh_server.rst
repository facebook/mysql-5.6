=========================================
 Installing and configuring a SSH server
=========================================

Many Linux distributions only install the ssh client by default. If you don't have the ssh server installed already, the easiest way of doing it is by using your distribution's packaging system: ::

     ubuntu$ sudo apt-get install openssh-server
  archlinux$ sudo pacman -S openssh

You may need to take a look at your distribution's documentation or search for a tutorial on the internet to configure it if you haven't done it before.


Some links of them are:

*  Debian - http://wiki.debian.org/SSH

*  Ubuntu - https://help.ubuntu.com/10.04/serverguide/C/openssh-server.html

*  Archlinux - https://wiki.archlinux.org/index.php/SSH

*  Fedora - http://docs.fedoraproject.org/en-US/Fedora/12/html/Deployment_Guide/s1-openssh-server-config.html

*  CentOS - http://www.centos.org/docs/5/html/Deployment_Guide-en-US/s1-openssh-server-config.html

*  RHEL - http://docs.redhat.com/docs/en-US/Red_Hat_Enterprise_Linux/6/html/Deployment_Guide/ch-OpenSSH.html
