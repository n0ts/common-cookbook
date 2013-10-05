#
# Cookbook Name:: common
# Recipe:: apache
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

#
# Package
#
%w{ bzip2 gzip lynx lzop }.each do |pkg|
  package pkg do
    action :install
  end
end


#
# Directory and File
#
file "/var/www/index.html" do
  content node['hostname']
  mode 0644
end

file "/etc/logrotate.d/apache2" do
  action :delete
end

directory node['apache']['log_dir'] do
  owner "root"
  group node['apache']['group']
  mode 0775
  action :create
end


#
# Modules
#
include_recipe "apache2::mod_status"
include_recipe "apache2::mod_rewrite"

%w{
  authz_groupfile
  cgid
}.each do |mod|
  apache_module mod do
    enable false
  end
end


#
# Congiguration
#
apache_site "default" do
  enable false
end

file "#{node['apache']['dir']}/conf.d/other-vhosts-access-log" do
  action :delete
  notifies :restart, resources(:service => "apache2")
end

# APACHE_ULIMIT_MAX_FILES = 65535
template "/etc/apache2/envvars" do
  mode 0644
  source "apache2-envvars.erb"
  only_if do ::FileTest.directory?("/etc/apache2") end
end

%w{
  001-default
  005-status
}.each do |conf|
  web_app conf do
    port, name = conf.split("-", 2)
    case name
    when "status"
      @params[:server_info] = true
      @params[:server_status] = true
      @params[:allow_from] = node['common']['apache']['allow_from']
    end

    template "httpd-#{conf}.conf.erb"
  end
end
