#
# Cookbook Name:: common
# Recipe:: mysql
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

directory "/var/log/mysql" do
  mode 0775
  owner "mysql"
  group "mysql"
  action :create
end

directory "/var/run/mysql" do
  mode 0775
  owner "mysql"
  group "mysql"
  action :create
end

file node[:mysql][:tunable][:log_error] do
  content ""
  mode 0640
  owner "mysql"
  group "mysql"
  action :create_if_missing
end

file node[:mysql][:tunable][:slow_query_log_file] do
  content ""
  mode 0640
  owner "mysql"
  group "mysql"
  action :create_if_missing
  not_if { node[:mysql][:tunable][:slow_query_log].empty? }
end

