#
# Cookbook Name:: common
# Recipe:: limits
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_limits, :contents => {} do
  template "/etc/security/limits.d/#{params[:name]}.conf" do
    cookbook "common"
    source "limits.conf.erb"
    variables :contents => params[:contents]
    owner "root"
    group "root"
    mode 0644
    only_if do ::FileTest.directory?("/etc/security/limits.d/") end
  end
end
