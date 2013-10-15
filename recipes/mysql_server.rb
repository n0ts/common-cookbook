#
# Cookbook Name:: common
# Recipe:: mysql_server
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#
# This recipe based on ospcode mysql::server
#

::Chef::Recipe.send(:include, Opscode::OpenSSL::Password)

include_recipe "mysql::client"

if Chef::Config[:solo]
  missing_attrs = %w{
    server_debian_password server_root_password server_repl_password
  }.select do |attr|
    node["mysql"][attr].nil?
  end.map { |attr| "node['mysql']['#{attr}']" }

  if !missing_attrs.empty?
    Chef::Application.fatal!([
        "You must set #{missing_attrs.join(', ')} in chef-solo mode.",
        "For more information, see https://github.com/opscode-cookbooks/mysql#chef-solo-note"
      ].join(' '))
  end
else
  # generate all passwords
  node.set_unless['mysql']['server_debian_password'] = secure_password
  node.set_unless['mysql']['server_root_password']   = secure_password
  node.set_unless['mysql']['server_repl_password']   = secure_password
  node.save
end

if platform_family?(%w{debian})

  directory "/var/cache/local/preseeding" do
    owner "root"
    group node['mysql']['root_group']
    mode 0755
    recursive true
  end

  execute "preseed mysql-server" do
    command "debconf-set-selections /var/cache/local/preseeding/mysql-server.seed"
    action :nothing
  end

  template "/var/cache/local/preseeding/mysql-server.seed" do
    cookbook "mysql"
    source "mysql-server.seed.erb"
    owner "root"
    group node['mysql']['root_group']
    mode "0600"
    notifies :run, "execute[preseed mysql-server]", :immediately
  end

  template "#{node['mysql']['conf_dir']}/debian.cnf" do
    cookbook "mysql"
    source "debian.cnf.erb"
    owner "root"
    group node['mysql']['root_group']
    mode "0600"
  end

end

if platform_family?('windows')
  package_file = node['mysql']['package_file']

  remote_file "#{Chef::Config[:file_cache_path]}/#{package_file}" do
    source node['mysql']['url']
    not_if { File.exists? "#{Chef::Config[:file_cache_path]}/#{package_file}" }
  end

  windows_package node['mysql']['server']['packages'].first do
    source "#{Chef::Config[:file_cache_path]}/#{package_file}"
  end

  def package(*args, &blk)
    windows_package(*args, &blk)
  end
end

# after installation mysql server package, remove my.cnf one time
node['mysql']['server']['packages'].each do |package_name|
  package package_name do
    action :install
    notifies :run, "execute[remove-mycnf]" , :immediately
  end
end

execute "remove-mycnf" do
  command "rm -f #{node['mysql']['conf_dir']}/my.cnf"
  user "root"
  action :nothing
end

unless platform_family?(%w{mac_os_x})

  [File.dirname(node['mysql']['pid_file']),
    File.dirname(node['mysql']['tunable']['slow_query_log']),
    node['mysql']['conf_dir'],
    node['mysql']['confd_dir'],
    node['mysql']['log_dir'],
    node['mysql']['data_dir']].each do |directory_path|
    directory directory_path do
      owner "mysql" unless platform? 'windows'
      group "mysql" unless platform? 'windows'
      action :create
      recursive true
    end
  end

  if platform_family? 'windows'
    require 'win32/service'

    windows_path node['mysql']['bin_dir'] do
      action :add
    end

    windows_batch "install mysql service" do
      command "\"#{node['mysql']['bin_dir']}\\mysqld.exe\" --install #{node['mysql']['service_name']}"
      not_if { Win32::Service.exists?(node['mysql']['service_name']) }
    end
  end

  skip_federated = case node['platform']
                   when 'fedora', 'ubuntu', 'amazon'
                     true
                   when 'centos', 'redhat', 'scientific'
                     node['platform_version'].to_f < 6.0
                   else
                     false
                   end
end

# Homebrew has its own way to do databases
if platform_family?(%w{mac_os_x})
  execute "mysql-install-db" do
    command "mysql_install_db --verbose --user=`whoami` --basedir=\"$(brew --prefix mysql)\" --datadir=#{node['mysql']['data_dir']} --tmpdir=/tmp"
    environment('TMPDIR' => nil)
    action :run
    creates "#{node['mysql']['data_dir']}/mysql"
  end
else
  execute 'mysql-install-db' do
    command "mysql_install_db"
    action :run
    not_if { File.exists?(node['mysql']['data_dir'] + '/mysql/user.frm') }
  end

  service "mysql" do
    service_name node['mysql']['service_name']
    if node['mysql']['use_upstart']
      provider Chef::Provider::Service::Upstart
    end
    supports :status => true, :restart => true, :reload => true
    action :enable
  end
end

# set the root password for situations that don't support pre-seeding.
# (eg. platforms other than debian/ubuntu & drop-in mysql replacements)
execute "assign-root-password" do
  command %Q["#{node['mysql']['mysqladmin_bin']}" -u root password '#{node['mysql']['server_root_password']}']
  action :run
  only_if %Q["#{node['mysql']['mysql_bin']}" -u root -e 'show databases;']
end

unless platform_family?(%w{mac_os_x})
  grants_path = node['mysql']['grants_path']

  begin
    t = resources("template[#{grants_path}]")
  rescue
    Chef::Log.info("Could not find previously defined grants.sql resource")
    t = template grants_path do
      cookbook "mysql"
      source "grants.sql.erb"
      owner "root" unless platform_family? 'windows'
      group node['mysql']['root_group'] unless platform_family? 'windows'
      mode "0600"
      action :create
    end
  end

  if platform_family? 'windows'
    windows_batch "mysql-install-privileges" do
      command "\"#{node['mysql']['mysql_bin']}\" -u root #{node['mysql']['server_root_password'].empty? ? '' : '-p' }\"#{node['mysql']['server_root_password']}\" < \"#{grants_path}\""
      action :nothing
      subscribes :run, resources("template[#{grants_path}]"), :immediately
    end
  else
    execute "mysql-install-privileges" do
      command %Q["#{node['mysql']['mysql_bin']}" -u root #{node['mysql']['server_root_password'].empty? ? '' : '-p' }'#{node['mysql']['server_root_password']}' < "#{grants_path}"]
      action :nothing
      subscribes :run, resources("template[#{grants_path}]"), :immediately
    end
  end

  # no replace my.cnf
  template "#{node['mysql']['conf_dir']}/my.cnf" do
    source "my.cnf.erb"
    owner "root" unless platform? 'windows'
    group node['mysql']['root_group'] unless platform? 'windows'
    mode "0644"
    variables :skip_federated => skip_federated
    action :create_if_missing
    notifies :run, "bash[reinstall-mysql-datadir]", :immediately
  end

  bash "reinstall-mysql-datadir" do
    user "root"
    code <<-EOH
service mysql stop
mv #{node['mysql']['data_dir']} #{node['mysql']['data_dir']}-orig
mkdir #{node['mysql']['data_dir']}
chown -R mysql:mysql  #{node['mysql']['data_dir']}
chmod 700 #{node['mysql']['data_dir']}
sudo -u mysql mysql_install_db
service mysql start
EOH
    timeout 600
    action :nothing
    notifies :run, "execute[assign-root-password]", :immediately
    not_if { ::FileTest.directory?("/var/lib/mysql-orig") }
  end

  service "mysql" do
    action :start
  end
end


#
# mymemcheck
#
remote_file "/usr/local/bin/mymemcheck" do
  source "https://gist.github.com/Craftworks/733390/raw/3ba64de584ad6bcdf8f4c76bedc7e1c3cb3538fa/mymemcheck"
  owner "root"
  group "root"
  mode 0755
  action :create
end


#
# mysql_secure_installation
#
package "expect"

template "/usr/local/etc/mysql_secure_installation-expect" do
  source "mysql_secure_installation-expect.erb"
  owner "root"
  group "root"
  mode 0755
  action :create_if_missing
  notifies :run, "execute[mysql_secure_installation]", :immediately
end

execute "mysql_secure_installation" do
  command "expect -f /usr/local/etc/mysql_secure_installation-expect"
  timeout 60
  user "root"
  action :nothing
end
