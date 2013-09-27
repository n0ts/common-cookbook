#
# Cookbook Name:: common
# Definition:: sysctl
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_sysctl, :content => "" do
  execute "sysctl-#{params[:name]}" do
    user "root"
    group "root"
    command "sysctl -p /etc/sysctl.d/#{params[:name]}"
    action :nothing
  end

  file "/etc/sysctl.d/#{params[:name]}" do
    owner "root"
    group "root"
    mode 0644
    content params[:content]
    action :create
    notifies :run, "execute[sysctl-#{params[:name]}]"
  end
end
