#
# Cookbook Name:: common
# Recipe:: default
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

template "/tmp/chef-node.txt" do
  source "chef-node.txt.erb"
end
