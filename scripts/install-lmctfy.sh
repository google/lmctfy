#!/bin/bash

# install the necessary dependencies and
# tools on ubuntu 12.04

# produce verbose output and exit on error
set -xe

# setup ubuntu package installs to not ask any questions.
export DEBIAN_FRONTEND=noninteractive

### dependency variables to install

re2_version=20140304
protobuf_version=2.6.1
compile_threads="$(lscpu  | awk '/^CPU\(s\)/ {print $2}')"

#### change nothing below
alias make="/usr/bin/make -j ${compile_threads}"
alias wget="/usr/bin/wget -q"

# update list of ubuntu packages.
sudo apt-get update

# install latest supported trusty (3.13) kernel
# FIXME: using untested kernel version to get around issue #38:
# https://github.com/google/lmctfy/issues/38
sudo apt-get install -y linux-image-generic-lts-trusty

# install general build tooling
sudo apt-get install -y git zip autoconf libtool python-software-properties pkg-config cmake

# setup g++ version 4.7 dependency
# based on : http://charette.no-ip.com:81/programming/2011-12-24_GCCv47/
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install -y gcc-4.7 g++-4.7
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.6 60 --slave /usr/bin/g++ g++ /usr/bin/g++-4.6
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.7 40 --slave /usr/bin/g++ g++ /usr/bin/g++-4.7 
sudo update-alternatives --set gcc /usr/bin/gcc-4.7

# setup protobuf dependency
if ! [ -e protobuf-${protobuf_version} ]; then
    wget https://github.com/google/protobuf/releases/download/v${protobuf_version}/protobuf-${protobuf_version}.tar.gz
    tar zxvf protobuf-${protobuf_version}.tar.gz
fi
pushd protobuf-${protobuf_version}
./autogen.sh
./configure
make
#make check
sudo make install
popd

# setup gflags dependency
[ -e gflags ] || git clone https://github.com/schuhschuh/gflags.git
pushd gflags
[ -e build ] || mkdir build
pushd build
cmake ..
make
sudo make install
popd
popd

# setup re2 dependencies
if ! [ -e re2 ] ; then
  wget https://re2.googlecode.com/files/re2-${re2_version}.tgz
  tar zxvf re2-${re2_version}.tgz
fi
pushd re2
make
sudo make install
popd

# setup apparmor dependency
sudo apt-get install -y apparmor libapparmor-dev

# setup go dependency
# HACK. stop this package from asking about upstream reporting and breaking the unattended install process (bad google)
echo "golang-go golang-go/dashboard boolean false" | sudo debconf-set-selections
sudo apt-get install -y --force-yes golang

# add /usr/local/lib to the list of places to look for libraries
echo "/usr/local/lib" |sudo tee /etc/ld.so.conf.d/usrlocal.conf
sudo ldconfig

# setup lmctfy
pushd lmctfy
make
make check
sudo make install
popd

# setup /sys/fs/cgroup mount
printf "tmpfs\t/sys/fs/cgroup\ttmpfs\tdefaults\t0\t0" | sudo tee -a /etc/fstab

# run lmctfy init on reboot
cat << 'EOF' | sudo tee /etc/rc.local
#!/bin/sh -e

/usr/local/bin/lmctfy init "
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/cpu'
    hierarchy:CGROUP_CPU hierarchy:CGROUP_CPUACCT
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/cpuset' hierarchy:CGROUP_CPUSET
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/freezer' hierarchy:CGROUP_FREEZER
  }
  cgroup_mount:{
    mount_path:'/sys/fs/cgroup/memory' hierarchy:CGROUP_MEMORY
  }"

exit 0
EOF

# reboot
sudo reboot
