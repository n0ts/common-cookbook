#
# Cookbook Name:: common
# Recipe:: activemq
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

#
# ActiveMQ port
# - 5672: AMQP(Advanced Message Queuing Protocol)
# - 8161: Web Console
# - 61613: STOMP (stomp://)
# - 61616: ActiveMQ's default port (openwrie://)
# - 61617: ActivwMQ's default ssl port (ssl://)
# - 61222: XMPP (xmpp://)
#

version = node['activemq']['version']
activemq_home = "#{node['activemq']['home']}/apache-activemq-#{version}"

#
# ActiveMQ home symlink
#
link "#{node['activemq']['home']}/activemq" do
  to activemq_home
end


#
# ActiveMQ user
#
common_user "activemq" do
  shell "/bin/nologin"
  home_dir activemq_home
end


#
# ActiveMQ home owner & group
#
bash "set-activemq-directory-owner" do
  user "root"
  code <<-EOH
chown -R activemq:activemq #{activemq_home}
chmod 755 #{activemq_home}
EOH
  only_if do ("%o" % File::stat(activemq_home).mode)[-3, 3] == "700" end
end


#
# ActiveMQ init.d
#
template "#{activemq_home}/bin/linux-x86-64/activemq" do
  source "activemq-bin.erb"
  owner "activemq"
  group "activemq"
  mode 0755
  action :create
  notifies :restart, "service[activemq]"
end


#
# ActiveMQ configuration
#
template "#{activemq_home}/conf/activemq.xml" do
  source "activemq.xml.erb"
  mode 0644
  owner "activemq"
  group "activemq"
  action :create
  notifies :restart, "service[activemq]"
end

template "/etc/logrotate.d/activemq" do
  source "activemq-logrotate.erb"
  owner "root"
  group "root"
  mode 0644
  action :create
end
