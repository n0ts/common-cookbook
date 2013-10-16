#
# Cookbook Name:: common
# Recipe:: mysql_server_before
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

#
# MySQL server
#
[File.dirname(node['mysql']['pid_file']),
 File.dirname(node['mysql']['tunable']['slow_query_log']),
 node['mysql']['conf_dir'],
 node['mysql']['confd_dir'],
 node['mysql']['log_dir'],
 node['mysql']['data_dir']].each do |directory_path|
  directory directory_path do
    owner "mysql" unless platform? 'windows'
    group "mysql" unless platform? 'windows'
    mode 0770
    action :create
    recursive true
  end
end

node['mysql']['server']['packages'].each do |package_name|
  package package_name do
    action :install
    notifies :run, "bash[after-install-mysql-server]", :immediately if node['mysql']['server']['packages'].last == package_name
  end
end

bash "after-install-mysql-server" do
  user "root"
  code <<-EOH
service mysql stop
rm -f "#{node['mysql']['conf_dir']}/my.cnf"
if [ "#{node['mysql']['data_dir']}" == "/var/lib/mysql" ]; then
  mv #{node['mysql']['data_dir']} #{node['mysql']['data_dir']}-orig
  mkdir #{node['mysql']['data_dir']}
  chown -R mysql:mysql #{node['mysql']['data_dir']}
  chmod 700 #{node['mysql']['data_dir']}
else
  mv "/var/lib/mysql" "/var/lib/mysql-orig"
  ln -s "#{node['mysql']['data_dir']}" /var/lib/mysql
fi
EOH
  action :nothing
  notifies :create, "template[#{node['mysql']['conf_dir']}/my.cnf]", :immediately
end

skip_federated = case node['platform']
                 when 'fedora', 'ubuntu', 'amazon'
                   true
                 when 'centos', 'redhat', 'scientific'
                   node['platform_version'].to_f < 6.0
                 else
                   false
                 end

template "#{node['mysql']['conf_dir']}/my.cnf" do
  source "my.cnf.erb"
  owner "root" unless platform? 'windows'
  group node['mysql']['root_group'] unless platform? 'windows'
  mode "0644"
  variables :skip_federated => skip_federated
  action :create_if_missing
  notifies :create, "template[/etc/apparmor.d/local/usr.sbin.mysqld]", :immediately if node['platform'] == 'ubuntu'
end

# AppArmor
template "/etc/apparmor.d/local/usr.sbin.mysqld" do
  source "apparmor-local-usr-sbin-mysqld.erb"
  owner "root"
  group "root"
  mode 0644
  action :create
  notifies :reload, "service[apparmor]", :immediately
  only_if { node['platform'] == 'ubuntu'}
end

service "apparmor" do
  supports :status => true, :restart => true, :reload => true
  action :nothing
  only_if { node['platform'] == 'ubuntu'}
  notifies :run, "execute[mysql-install-db]", :immediately
end


execute "mysql-install-db" do
  command "mysql_install_db"
  action :run
  not_if { File.exists?(node['mysql']['data_dir'] + '/mysql/user.frm') }
  notifies :run, "bash[wait-mysql-server-starting]", :immediately
end

bash "wait-mysql-server-starting" do
  user "root"
  code <<-EOH
service mysql start
count=0
while true; do
    mysql -u root
    RETVAL=$?
    if [ $RETVAL == 0 ]; then
        break
    fi
    sleep 60
    count=`expr $count + 1`
    if [ $count == 60 ]; then
        echo "MySQL server is not connected."
        exit 1
    fi
done
EOH
  timeout 3600
  action :nothing
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

