#
# Cookbook Name:: common
# Recipe:: postfix
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

common_postfix_postmap "virtual_alias_maps" do
  content "#{node[:hostname]}.#{node[:postfix][:main][:mydomain]} root"
end
