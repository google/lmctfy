Vagrant.configure("2") do |config|
    config.vm.box = "ubuntu/precise64"

    config.vm.provider "virtualbox" do |v|
        v.memory = 1024
        v.cpus = 2
    end

    config.vm.synced_folder ".", "/home/vagrant/lmctfy"
    config.vm.provision :shell, :privileged => false, :path => "scripts/install-lmctfy.sh"

end
